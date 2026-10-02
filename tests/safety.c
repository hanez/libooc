/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/*
 * libooc safety regression tests.
 *
 * Checks cover rejected metadata and arguments, overlapping field assignments,
 * owned-pointer replacement, reference-count limits, the example constructors,
 * two subclasses of one base driven through a single call site, and a
 * three-level hierarchy whose branches do not see each other. Assertions must be
 * enabled: NDEBUG removes checks and API calls inside assert(), leaving an
 * incomplete test run. Use AddressSanitizer or Valgrind as well to detect
 * invalid memory accesses, invalid frees, and leaks.
 */

#include <ooc/ooc.h>
#include "cat.h"
#include "dog.h"
#include "garfield.h"
#include "snoopy.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Object used by check_fields(). The byte array is copied by value; the owned
 * pointer is freed on replacement and destruction. Both follow the header.
 */
struct sample {
    ooc_object object;
    char bytes[8];
    void *owned;
};

/* Counts destructor calls, so a test can tell a release from a no-op. */
static int destroyed;

/*
 * Destructor for the sample object. Verifies that a zero reference count during
 * destruction prevents retention and makes a nested release a no-op, then
 * frees the owned allocation and records the destructor call.
 */
static void destroy(ooc_object *object)
{
    struct sample *self = (struct sample *)object;

    /* A destructor must not resurrect or recursively free the object. */
    assert(ooc_retain(object) == NULL);
    ooc_release(object);
    free(self->owned);
    ++destroyed;
}

/*
 * Check field validation, overlapping copies, ownership, and reference counts.
 *
 * The table contains two valid descriptors and six invalid ones; the rejected
 * names also include an absent field. Both accessors must reject fields in the
 * header, out-of-bounds or overflowing ranges, zero-sized fields, and an owned
 * field with the wrong pointer width. NULL names and source values are checked.
 *
 * Value copies cover identical and partially overlapping source storage;
 * memcpy() would have undefined behavior for these overlapping ranges. Owned
 * assignments cover the first allocation, assigning the slot back to itself,
 * and replacement with a second allocation. Memory checkers can detect invalid
 * frees or leaks that the return-value assertions alone cannot establish.
 *
 * Retaining at SIZE_MAX must fail without changing the count. After resetting
 * the count to one, a successful retain raises it to two: the first release
 * keeps the object alive and the second calls destroy() exactly once.
 */
static void check_fields(void)
{
    const ooc_field fields[] = {
        { "bytes", offsetof(struct sample, bytes), 8, 0 },
        { "owned", offsetof(struct sample, owned), sizeof(void *), 1 },
        { "header", 0, sizeof(void *), 0 },
        { "past", sizeof(struct sample), 1, 0 },
        { "wrap", SIZE_MAX, 2, 0 },
        { "huge", offsetof(struct sample, bytes), SIZE_MAX, 0 },
        { "empty", offsetof(struct sample, bytes), 0, 0 },
        { "bad_owned", offsetof(struct sample, bytes), 1, 1 },
        { NULL, 0, 0, 0 }
    };
    /*
     * The sample class, sized for the struct and with no base. `destroy` is the
     * counting destructor above, so the release at the end of this function can
     * be told apart from a no-op, and the field table is the local one that mixes
     * valid descriptors with invalid ones.
     */
    const ooc_class class = { sizeof(struct sample), destroy, NULL, fields };
    const char *invalid[] = { "header", "past", "wrap", "huge", "empty", "bad_owned", "absent" };
    struct sample *self = ooc_new(&class);
    char initial[8] = "abcdefg";
    void *replacement = malloc(8);
    void *second = malloc(8);
    size_t i;

    assert(self && replacement && second);
    assert(ooc_set(self, "bytes", initial) == 0);
    assert(ooc_set(self, "bytes", self->bytes) == 0);
    /* Source partially overlaps the destination but fits in the object. */
    assert(ooc_set(self, "bytes", self->bytes + 1) == 0);
    assert(memcmp(self->bytes, "bcdefg\0", 7) == 0);
    for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(ooc_get(self, invalid[i]) == NULL);
        assert(ooc_set(self, invalid[i], initial) == -1);
    }
    assert(ooc_get(self, NULL) == NULL);
    assert(ooc_set(self, "bytes", NULL) == -1);
    assert(ooc_set(self, "owned", &replacement) == 0);
    assert(ooc_set(self, "owned", ooc_get(self, "owned")) == 0);
    assert(ooc_set(self, "owned", &second) == 0);
    assert(self->owned == second);
    self->object.refs = SIZE_MAX;
    assert(ooc_retain(self) == NULL);
    assert(self->object.refs == SIZE_MAX);
    self->object.refs = 1;
    assert(ooc_retain(self) == self);
    ooc_release(self);
    assert(destroyed == 0);
    ooc_release(self);
    assert(destroyed == 1);
}

/*
 * Check rejection of NULL or undersized classes, a base larger than its
 * subclass, a two-class cycle, and a self-cycle. Field lookup through the
 * two-class cycle must also return NULL instead of looping indefinitely, and
 * ooc_is_a() through a self-cycle must answer false instead of looping, since
 * it walks the chain.
 *
 * Stack metadata is changed only between calls, while no allocated objects
 * refer to it. A synthetic stack header exercises narrowly defined guards:
 * a NULL class cannot match ooc_is_a(), retaining a zero count fails, and
 * releasing a zero count or NULL does nothing. These checks do not imply that
 * arbitrary stack objects or invalid pointers are supported by the runtime.
 */
static void check_classes(void)
{
    ooc_class first = { sizeof(struct sample), NULL, NULL, NULL };
    ooc_class second = { sizeof(struct sample), NULL, &first, NULL };
    ooc_object fake = { NULL, 0 };

    assert(ooc_new(NULL) == NULL);
    first.size = sizeof(ooc_object) - 1;
    assert(ooc_new(&first) == NULL);
    first.size = sizeof(struct sample) + 1;
    assert(ooc_new(&second) == NULL);
    first.size = sizeof(struct sample);
    first.super = &second;
    assert(ooc_new(&first) == NULL);
    fake.class = &first;
    assert(ooc_get(&fake, "missing") == NULL);
    first.super = &first;
    assert(ooc_new(&first) == NULL);
    /* A self-referential chain must answer rather than loop, now that a type
       check walks it. */
    assert(!ooc_is_a(&fake, &first));
    fake.class = NULL;
    assert(!ooc_is_a(&fake, NULL));
    assert(ooc_retain(&fake) == NULL);
    ooc_release(&fake);
    ooc_release(NULL);
}

/*
 * Check inherited field lookup and access restrictions on a constructed Dog:
 * age resolves to Animal's storage, _id refuses writes, and __legs refuses
 * reads. NULL and repeated animal_init() calls must fail, leaving the existing
 * name unchanged on repeated initialization.
 *
 * Setting the owned name and breed to NULL must succeed. Speaking afterwards
 * exercises NULL-safe output, then releasing the dog exercises cleanup. The
 * final constructor checks reject a NULL name and a NULL breed; the latter
 * fails after base initialization and must clean up the partially built dog.
 * Memory checkers verify that these paths do not leak or free invalid storage.
 */
static void check_example(void)
{
    Dog *dog = dog_new("Rex", 5, "Shepherd");
    char *empty = NULL;
    int value = 9;
    assert(dog);
    assert(ooc_get(dog, "age") == &dog->animal.age);
    assert(ooc_set(dog, "_id", &value) == -1);
    assert(ooc_get(dog, "__legs") == NULL);
    assert(animal_init(NULL, "name", 1) == -1);
    assert(animal_init(&dog->animal, "again", 1) == -1);
    assert(strcmp(dog->animal.name, "Rex") == 0);
    assert(ooc_set(dog, "name", &empty) == 0);
    assert(ooc_set(dog, "breed", &empty) == 0);
    animal_speak(&dog->animal);
    ooc_release(dog);
    assert(animal_new(NULL, 0) == NULL);
    assert(dog_new("name", 0, NULL) == NULL);
}

/*
 * Check a Cat and a Dog living side by side, which is the case the second
 * subclass exists to cover.
 *
 * A Cat resolves "colour" to its own storage while "age" still resolves to
 * Animal's, and its own "_lives" refuses writes while remaining readable, so
 * the underscore rules are checked on a field the subclass declared rather than
 * one it inherited. The hidden "__legs" stays out of reach from either object.
 *
 * Replacing the owned colour with a second allocation must release the first;
 * only a memory checker can see that the string cat_new() allocated is gone
 * rather than leaked. The replacement is heap memory, since a stack buffer
 * handed to an owned field would be freed by the destructor.
 *
 * ooc_is_a() answers for a base class as well as for the runtime type, so a Cat
 * is a Cat and an Animal but not a Dog, while the exact type is read from the
 * object's own class member. Both objects are then spoken through the same
 * Animal pointer, where the printed lines are the evidence that each reached
 * its own vtable. Clearing both owned strings to NULL makes the speak
 * implementations fall back instead of passing NULL to printf(), and releasing
 * both objects afterwards must free what is left without a double free.
 *
 * The final constructor check rejects a NULL colour, which fails after base
 * initialization and must clean up the partially built cat.
 */
static void check_subclasses(void)
{
    Dog *dog = dog_new("Rex", 5, "Shepherd");
    Cat *cat = cat_new("Mia", 3, "tabby");
    Animal *dog_view;
    Animal *cat_view;
    char *colour = malloc(sizeof("calico"));
    char *empty = NULL;
    int value = 3;

    assert(dog && cat && colour);
    memcpy(colour, "calico", sizeof("calico"));

    /* A subclass field and an inherited one, both resolved to real storage. */
    assert(ooc_get(cat, "colour") == &cat->colour);
    assert(ooc_get(cat, "age") == &cat->animal.age);
    assert(ooc_get(cat, "__legs") == NULL);
    assert(ooc_get(dog, "__legs") == NULL);

    /* The subclass's own private field: readable, refused to write, unchanged. */
    assert(*(int *)ooc_get(cat, "_lives") == 9);
    assert(ooc_set(cat, "_lives", &value) == -1);
    assert(*(int *)ooc_get(cat, "_lives") == 9);

    /* Owned replacement on a subclass field releases the first allocation. */
    assert(ooc_set(cat, "colour", &colour) == 0);
    assert(strcmp(cat->colour, "calico") == 0);
    assert(ooc_get(cat, "colour") == &cat->colour);

    /* Subtype test walks the chain; the exact type comes from `class`. */
    assert(ooc_is_a(cat, &Cat_class));
    assert(ooc_is_a(cat, &Animal_class));
    assert(!ooc_is_a(cat, &Dog_class));
    assert(ooc_is_a(dog, &Dog_class));
    assert(ooc_is_a(dog, &Animal_class));
    assert(!ooc_is_a(dog, &Cat_class));
    assert(cat->animal.object.class == &Cat_class);
    assert(dog->animal.object.class != &Animal_class);

    /* One call site, two vtables: each object answers in its own voice. */
    dog_view = ooc_retain((Animal *)dog);
    cat_view = ooc_retain((Animal *)cat);
    assert(dog_view && cat_view);
    animal_speak(dog_view);
    animal_speak(cat_view);

    /* NULL-safe speak output, then cleanup of both objects. */
    assert(ooc_set(cat, "colour", &empty) == 0);
    assert(ooc_set(cat, "name", &empty) == 0);
    assert(ooc_set(dog, "breed", &empty) == 0);
    animal_speak(cat_view);
    ooc_release(cat_view);
    ooc_release(cat);
    ooc_release(dog_view);
    ooc_release(dog);

    assert(cat_new("name", 0, NULL) == NULL);
}

/*
 * Check a three-level hierarchy, with a subclass of a subclass on each branch.
 *
 * The point of the depth is that lookup crosses two class records, so "colour"
 * is found at Cat_class and "name" at Animal_class, and neither is shadowed or
 * lost by Garfield sitting in between. A field declared on the other branch must
 * not resolve at all: an absent name is a NULL from ooc_get() and -1 from
 * ooc_set(), which is the safe answer rather than a write at a wrong offset.
 *
 * The underscore rules have to hold across the extra level too. `_meals` and
 * `_lives` are both readable and both refuse writes, each through the class that
 * declared it, while `__flights` and `__legs` are unreadable as well and only the
 * accessors reach them.
 *
 * Replacing an owned field three levels down has to release the string that was
 * there before, and the destructor then has three allocations to free rather than
 * one. Only a memory checker can see whether the middle one was freed: a missing
 * free at this depth is silent, since the object still works perfectly well.
 * That is what the last release below is checking.
 *
 * Both objects are also spoken through one Animal * each, so the printed lines
 * are the evidence that a three-level override reaches its own implementation.
 * Repeated initialisation is refused at every level, leaving the existing members
 * intact, and the constructor checks reject a NULL argument at each step: the
 * food and the imagination fail last, and the colour and the breed fail inside the
 * base part, which is a different cleanup path again.
 */
static void check_deep_hierarchy(void)
{
    Garfield *garfield = garfield_new("Garfield", 4, "orange", "lasagna");
    Snoopy   *snoopy  = snoopy_new("Snoopy", 3, "beagle", "his red baron");
    Animal   *garfield_view;
    Animal   *snoopy_view;
    char     *food = malloc(sizeof("pizza"));
    char     *dream = malloc(sizeof("a nap"));
    char     *empty = NULL;
    int       value = 7;

    assert(garfield && snoopy && food && dream);
    memcpy(food, "pizza", sizeof("pizza"));
    memcpy(dream, "a nap", sizeof("a nap"));

    /* Each level's own field, and one found two records up. */
    assert(ooc_get(garfield, "favourite_food") == &garfield->favourite_food);
    assert(ooc_get(garfield, "colour") == &garfield->cat.colour);
    assert(ooc_get(garfield, "name") == &garfield->cat.animal.name);
    assert(ooc_get(snoopy, "imagination") == &snoopy->imagination);
    assert(ooc_get(snoopy, "breed") == &snoopy->dog.breed);
    assert(ooc_get(snoopy, "name") == &snoopy->dog.animal.name);

    /* The other branch's field is absent from this chain, not misread. */
    assert(ooc_get(garfield, "imagination") == NULL);
    assert(ooc_set(garfield, "imagination", dream) == -1);
    assert(ooc_get(snoopy, "colour") == NULL);
    assert(ooc_set(snoopy, "colour", dream) == -1);
    assert(ooc_get(snoopy, "favourite_food") == NULL);
    assert(ooc_get(garfield, "breed") == NULL);

    /* Private in the declaring class, readable and unwritable at any depth. */
    assert(*(int *)ooc_get(garfield, "_meals") == 1);
    assert(ooc_set(garfield, "_meals", &value) == -1);
    assert(*(int *)ooc_get(garfield, "_meals") == 1);
    assert(ooc_set(garfield, "_lives", &value) == -1);
    assert(*(int *)ooc_get(garfield, "_lives") == 9);

    /* Two underscores withhold the field from ooc_get() as well. */
    assert(ooc_get(garfield, "__legs") == NULL);
    assert(ooc_get(snoopy, "__flights") == NULL);
    assert(ooc_set(snoopy, "__flights", &value) == -1);
    assert(snoopy_flights(snoopy) == 0);
    assert(garfield_meals(garfield) == 1);
    assert(garfield_meals(NULL) == 0);
    assert(snoopy_flights(NULL) == 0);

    /* Owned replacement on the deepest own field, and on an inherited one. */
    assert(ooc_set(garfield, "favourite_food", &food) == 0);
    assert(strcmp(garfield->favourite_food, "pizza") == 0);
    assert(ooc_set(snoopy, "imagination", &dream) == 0);
    assert(strcmp(snoopy->imagination, "a nap") == 0);

    /*
     * Subtype tests walk the whole chain, so a three-level object answers for
     * every ancestor, while the sibling branch stays unrelated at any depth.
     */
    assert(ooc_is_a(garfield, &Garfield_class));
    assert(ooc_is_a(garfield, &Cat_class));
    assert(ooc_is_a(garfield, &Animal_class));
    assert(!ooc_is_a(garfield, &Dog_class));
    assert(!ooc_is_a(garfield, &Snoopy_class));
    assert(ooc_is_a(snoopy, &Snoopy_class));
    assert(ooc_is_a(snoopy, &Dog_class));
    assert(ooc_is_a(snoopy, &Animal_class));
    assert(!ooc_is_a(snoopy, &Cat_class));
    assert(!ooc_is_a(snoopy, &Garfield_class));

    /* The exact type is the deepest record, with no walk involved. */
    assert(garfield->cat.animal.object.class == &Garfield_class);
    assert(snoopy->dog.animal.object.class == &Snoopy_class);

    /* A three-level override, reached through one Animal pointer each. */
    garfield_view = ooc_retain(&garfield->cat.animal);
    snoopy_view = ooc_retain(&snoopy->dog.animal);
    assert(garfield_view && snoopy_view);
    assert(garfield_view->vtable->speak != snoopy_view->vtable->speak);
    animal_speak(garfield_view);
    animal_speak(snoopy_view);

    /* Repeated initialisation is refused at every level, members intact. */
    assert(garfield_init(garfield, "again", 1, "red", "pizza") == -1);
    assert(strcmp(garfield->cat.animal.name, "Garfield") == 0);
    assert(snoopy_init(snoopy, "again", 1, "poodle", "food") == -1);
    assert(strcmp(snoopy->dog.animal.name, "Snoopy") == 0);
    assert(garfield_init(NULL, "name", 0, "colour", "food") == -1);
    assert(snoopy_init(NULL, "name", 0, "breed", "dream") == -1);
    assert(cat_init(NULL, "name", 0, "colour") == -1);
    assert(dog_init(NULL, "name", 0, "breed") == -1);
    assert(cat_init(&garfield->cat, "again", 1, "red") == -1);
    assert(dog_init(&snoopy->dog, "again", 1, "poodle") == -1);

    /*
     * Cleared strings must not reach printf(), and the destructor then frees what
     * is left: three allocations here, so a memory checker is the only thing that
     * can catch a missing free at the middle level.
     */
    assert(ooc_set(garfield, "favourite_food", &empty) == 0);
    assert(ooc_set(garfield, "colour", &empty) == 0);
    assert(ooc_set(snoopy, "imagination", &empty) == 0);
    assert(ooc_set(snoopy, "breed", &empty) == 0);
    animal_speak(garfield_view);
    ooc_release(garfield_view);
    ooc_release(garfield);
    ooc_release(snoopy_view);
    ooc_release(snoopy);

    /* NULL arguments at each constructor step, from both branches. */
    assert(garfield_new("name", 0, NULL, "food") == NULL);
    assert(garfield_new("name", 0, "colour", NULL) == NULL);
    assert(snoopy_new("name", 0, NULL, "dream") == NULL);
    assert(snoopy_new("name", 0, "breed", NULL) == NULL);
}

/*
 * Run all five groups of checks with assertions enabled. A successful run
 * returns 0 and prints the speak output of the example animals, including the
 * NULL-safe fallbacks. A failed assertion aborts instead of returning normally; a
 * memory checker may report additional failures. Compiling with NDEBUG disables
 * the assertion checks.
 */
int main(void)
{
    check_fields();
    check_classes();
    check_example();
    check_subclasses();
    check_deep_hierarchy();
    return 0;
}
