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
 * and two subclasses of one base driven through a single call site. Assertions
 * must be enabled: NDEBUG removes checks and API calls inside assert(), leaving
 * an incomplete test run. Use AddressSanitizer or Valgrind as well to detect
 * invalid memory accesses, invalid frees, and leaks.
 */

#include <ooc/ooc.h>
#include "cat.h"
#include "dog.h"

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
 * Run all four groups of checks with assertions enabled. A successful run
 * returns 0 and prints the NULL-safe speak output of both example animals. A
 * failed assertion aborts instead of returning normally; a memory checker may
 * report additional failures. Compiling with NDEBUG disables the assertion
 * checks.
 */
int main(void)
{
    check_fields();
    check_classes();
    check_example();
    check_subclasses();
    return 0;
}
