/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2.0
 */

/*
 * Walks through the libooc life cycle four times, once per class in the
 * hierarchy.
 *
 * The Dog walk covers the whole API: allocation, a second reference through a
 * base class, reading and writing fields by name, the private field rules, and
 * the virtual destructor. The Cat walk then repeats the same calls against a
 * second subclass, so the output makes the one thing that is not visible in the
 * source clear: `animal_speak` is a single call site and still reaches Dog's
 * implementation for one object and Cat's for the other.
 *
 * Garfield and Snoopy go one level deeper, each on its own branch, and they are
 * what turn a list of classes into a hierarchy worth looking at. The last walk
 * puts all four objects through one Animal * and asks what each really is, which
 * is the question a caller with no static type information can only answer by
 * asking.
 */

#include "cat.h"
#include "dog.h"
#include "garfield.h"
#include "snoopy.h"

#include <ooc/ooc.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Copy `text` onto the heap, because a field that owns its value needs heap.
 *
 * The example classes have their own private copies of this; it is duplicated
 * here rather than shared because main.c is a caller, not part of the hierarchy,
 * and has no business reaching into a class' internals for it.
 *
 * Returns NULL if `text` is NULL or the allocation fails.
 */
static char *copy_of(const char *text)
{
    size_t len;
    char *copy;

    if (!text)
        return NULL;

    len = strlen(text);

    if (len == SIZE_MAX)
        return NULL;

    copy = malloc(len + 1);

    if (copy)
        memcpy(copy, text, len + 1);

    return copy;
}

/*
 * The Dog walk.
 *
 * One object taken through the whole life cycle: created, shared through a base
 * class reference, written and read by field name, spoken through, and
 * released. Every reference the object collects has to be given back, so both
 * `dog` and `animal` are released on the way out, including on the error paths.
 *
 * Returns 0 when the walk completes and -1 if a step the example relies on was
 * refused.
 */
static int visit_dog(void)
{
    Dog *dog;
    Animal *animal;
    char *name;
    int birthday = 6;
    int weight = 45;

    dog = dog_new("Rex", 5, "German Shepherd");
    if (!dog) {
        fprintf(stderr, "failed to create dog\n");
        return -1;
    }

    /* The count is now two, so both references have to be released. */
    animal = ooc_retain((Animal *)dog);
    if (!animal) {
        ooc_release(dog);
        return -1;
    }

    /*
     * Fields are reachable by name alone, with no struct definition in sight:
     * "breed" is Dog's own field, "name" and "age" are inherited from Animal.
     *
     * ooc_get() hands back the address of the field, so a string field is read
     * in two steps -- the slot first, the string it points to second.
     */
    printf("%s is a %d year old %s.\n",
           *(char **)ooc_get(dog, "name"),
           *(int *)ooc_get(dog, "age"),
           *(char **)ooc_get(dog, "breed"));

    /* ooc_set() copies a value into the named field. */
    if (ooc_set(dog, "age", &birthday) != 0) {
        fprintf(stderr, "failed to set age\n");
        goto fail;
    }

    printf("%s just turned %d.\n", *(char **)ooc_get(dog, "name"),
           *(int *)ooc_get(dog, "age"));

    /*
     * "name" is a field the class owns, so assigning a new one hands it over:
     * the replacement is copied in and the string it displaces is released
     * here, leaving nothing to free by hand.
     *
     * ooc_set() takes the address of the value, so a pointer field is assigned
     * through a pointer to the pointer -- and the new value has to be heap
     * memory rather than a literal, because the object would otherwise own
     * something free() cannot release.
     */
    name = copy_of("Bella");
    if (!name || ooc_set(dog, "name", &name) != 0) {
        fprintf(stderr, "failed to set name\n");
        free(name);
        goto fail;
    }

    printf("%s was renamed.\n", *(char **)ooc_get(dog, "name"));

    /* An unknown name is refused rather than written out of bounds. */
    if (ooc_set(dog, "weight", &weight) != 0)
        printf("no such field: weight\n");

    /*
     * A name starting with an underscore belongs to the class that declared
     * it, so the setter turns it away. The field is still readable by name,
     * which is what lets a class show its own state without handing out a way
     * to change it -- note that "_id" is inherited from Animal, and the
     * refusal follows the name to whichever class in the chain declared it.
     */
    if (ooc_set(dog, "_id", &weight) != 0)
        printf("_id is private, and reads back as %d\n",
               *(int *)ooc_get(dog, "_id"));

    /*
     * Two leading underscores go further and are not readable either:
     * ooc_get() finds the field and then withholds it, so there is no way in
     * from here at all. An accessor is the only way to the value, and both
     * "_id" and "__legs" now report the same through the class instead.
     */
    printf("__legs is readable by name: %s\n",
           ooc_get(dog, "__legs") ? "yes" : "no");
    printf("__legs is writable by name: %s\n",
           ooc_set(dog, "__legs", &weight) == 0 ? "yes" : "no");
    printf("and the class still reports %d legs, id %d.\n",
           animal_legs((Animal *)dog), animal_id((Animal *)dog));

    animal_speak(animal);

    ooc_release(dog);
    ooc_release(animal);

    return 0;

fail:
    ooc_release(animal);
    ooc_release(dog);
    return -1;
}

/*
 * The Cat walk.
 *
 * The same calls as visit_dog(), against a second subclass, and that is the
 * point of having it: nothing below is Cat-specific. Field lookup walks the
 * chain to Animal for "name" and "age" and stops at Cat for "colour", the
 * owned-field and private-field rules behave identically, and the destructor
 * releases both strings because Cat registered its own.
 *
 * The runtime type is asked about explicitly at the end, in both directions.
 * ooc_is_a() answers yes for the cat and for the Animal it derives from and no
 * for its sibling, and the exact type comes from the object's own class
 * member.
 *
 * Returns 0 when the walk completes and -1 if a step the example relies on was
 * refused.
 */
static int visit_cat(void)
{
    Cat *cat;
    Animal *animal;
    char *colour;
    int weight = 45;

    cat = cat_new("Mia", 3, "tabby");
    if (!cat) {
        fprintf(stderr, "failed to create cat\n");
        return -1;
    }

    /* A second reference again, this time on an object of another type. */
    animal = ooc_retain((Animal *)cat);
    if (!animal) {
        ooc_release(cat);
        return -1;
    }

    printf("%s is a %d year old %s with %d lives.\n",
           *(char **)ooc_get(cat, "name"),
           *(int *)ooc_get(cat, "age"),
           *(char **)ooc_get(cat, "colour"),
           *(int *)ooc_get(cat, "_lives"));

    /*
     * "colour" is owned, exactly as Dog's breed is, so a replacement releases
     * the string that was there. Same call, same rules, a field the subclass
     * declared for itself.
     */
    colour = copy_of("calico");
    if (!colour || ooc_set(cat, "colour", &colour) != 0) {
        fprintf(stderr, "failed to set colour\n");
        free(colour);
        goto fail;
    }

    printf("%s is now a %s.\n", *(char **)ooc_get(cat, "name"),
           *(char **)ooc_get(cat, "colour"));

    /*
     * The underscore rules do not care which class in the chain declared the
     * field. Cat's own `_lives` is readable and not writable, the same as
     * Animal's `_id` reached through a Dog, and Animal's hidden `__legs` is
     * still out of reach from here.
     */
    if (ooc_set(cat, "_lives", &weight) != 0)
        printf("_lives is private, and reads back as %d\n",
               *(int *)ooc_get(cat, "_lives"));
    printf("__legs is readable from a cat: %s\n",
           ooc_get(cat, "__legs") ? "yes" : "no");
    printf("and the class still reports %d legs.\n", animal_legs(animal));

    /*
     * The runtime type is what the object carries, not what the cast says.
     *
     * ooc_is_a() walks the inheritance chain, so a cat answers yes for Cat and
     * for the Animal it derives from, and no for its sibling Dog. Asking about
     * a base class is the useful direction: it is how a caller holding an
     * Animal * asks "is this one of mine" without knowing the answer in
     * advance.
     */
    printf("a cat is a Cat: %s, an Animal: %s, a Dog: %s\n",
           ooc_is_a(cat, &Cat_class) ? "yes" : "no",
           ooc_is_a(cat, &Animal_class) ? "yes" : "no",
           ooc_is_a(cat, &Dog_class) ? "yes" : "no");

    /*
     * Asking "exactly which type" is a different question, and needs no chain
     * walk: the class record is a public member of the object, so a single
     * comparison answers it. This is also what a switch over a tag would use.
     */
    printf("and it is exactly an Animal: %s\n",
           cat->animal.object.class == &Animal_class ? "yes" : "no");

    /* One call site, and the vtable decides which speak() runs. */
    animal_speak(animal);

    ooc_release(cat);
    ooc_release(animal);

    return 0;

fail:
    ooc_release(animal);
    ooc_release(cat);
    return -1;
}

/*
 * The Garfield walk: a subclass of a subclass.
 *
 * Nothing here is new in kind. The object is created and released the same way
 * as a Dog, fields resolve by name the same way, and the ownership and underscore
 * rules are the library's rather than the class's. The differences are all
 * consequences of the extra level, and they are what the walk is for.
 *
 * Lookup now crosses two class records instead of one. "favourite_food" and
 * "_meals" are Garfield's own, "colour" and "_lives" are found at Cat_class, and
 * "name" and "age" are found at Animal_class -- so a name declared two levels up
 * is as reachable as one declared immediately above.
 *
 * The private rules also cross the level unchanged, and that is worth seeing:
 * `_meals` belongs to Garfield, `_lives` to Cat, and a caller can read both by
 * name but write neither. A subclass sitting between the caller and the
 * declaration does not weaken the rule.
 *
 * Returns 0 when the walk completes and -1 if a step the example relies on was
 * refused.
 */
static int visit_garfield(void)
{
    Garfield *garfield;
    Cat *cat_view;
    Animal *animal;
    char *food;
    int servings = 7;

    garfield = garfield_new("Garfield", 4, "orange", "lasagna");
    if (!garfield) {
        fprintf(stderr, "failed to create garfield\n");
        return -1;
    }

    /*
     * Upcasting is unchecked in C, so these two casts are just arithmetic that
     * happens to be zero: every class in the chain embeds the one above it as
     * its first member. A handle to the middle of the chain works exactly like a
     * handle to the top or the bottom, which is why the same lookup and the same
     * vtable work through any of them.
     */
    cat_view = &garfield->cat;
    animal = &garfield->cat.animal;

    printf("through a Cat handle, the same object answers as %s.\n",
           *(char **)ooc_get(cat_view, "name"));

    printf("%s is a %d year old %s %s who has eaten %d meal%s.\n",
           *(char **)ooc_get(garfield, "name"),
           *(int *)ooc_get(garfield, "age"),
           *(char **)ooc_get(garfield, "colour"),
           *(char **)ooc_get(garfield, "favourite_food"),
           garfield_meals(garfield),
           garfield_meals(garfield) == 1 ? "" : "s");

    /*
     * The owned field is the class's own, so replacing it hands the old string
     * over and frees it -- the same rule as a breed or a colour, one level down.
     */
    food = copy_of("pizza");
    if (!food || ooc_set(garfield, "favourite_food", &food) != 0) {
        fprintf(stderr, "failed to set favourite food\n");
        free(food);
        goto fail;
    }

    printf("%s now prefers %s.\n", *(char **)ooc_get(garfield, "name"),
           *(char **)ooc_get(garfield, "favourite_food"));

    /* Both private fields are readable and neither is writable. */
    if (ooc_set(garfield, "_meals", &servings) != 0)
        printf("_meals is private, and reads back as %d\n",
               *(int *)ooc_get(garfield, "_meals"));
    if (ooc_set(garfield, "_lives", &servings) != 0)
        printf("_lives is Cat's, and reads back as %d\n",
               *(int *)ooc_get(garfield, "_lives"));
    printf("__legs is readable from three levels down: %s\n",
           ooc_get(garfield, "__legs") ? "yes" : "no");

    /*
     * The exact type is the deepest class record, reached without a walk, and
     * the subtype test walks all three links to say the same thing the long way.
     */
    printf("a garfield is exactly a Garfield: %s, a Cat: %s, an Animal: %s\n",
           garfield->cat.animal.object.class == &Garfield_class ? "yes" : "no",
           ooc_is_a(garfield, &Cat_class) ? "yes" : "no",
           ooc_is_a(garfield, &Animal_class) ? "yes" : "no");

    /* The other branch of the hierarchy is not reachable from here. */
    printf("a garfield is a Snoopy: %s, and has an imagination: %s\n",
           ooc_is_a(garfield, &Snoopy_class) ? "yes" : "no",
           ooc_get(garfield, "imagination") ? "yes" : "no");

    /* One call site, and the vtable decides which speak() runs. */
    animal_speak(animal);

    /*
     * One reference only, despite the three handles to it: cat_view and animal
     * alias the same storage, so releasing any one of them twice would destroy
     * the object early. A pointer to a base class is a view, not a new claim.
     */
    ooc_release(garfield);

    return 0;

fail:
    ooc_release(garfield);
    return -1;
}

/*
 * The Snoopy walk: the other branch, and a hidden field one level down.
 *
 * The same calls as visit_garfield(), against a Dog subclass rather than a Cat
 * one, so the differences are the ones the fork causes. `__flights` carries two
 * underscores, which ooc_get() withholds as well as ooc_set() refusing to
 * write it, so snoopy_flights() is the only way in -- where Garfield's `_meals`
 * is still readable by name.
 *
 * Returns 0 when the walk completes and -1 if a step the example relies on was
 * refused.
 */
static int visit_snoopy(void)
{
    Snoopy *snoopy;
    Animal *animal;
    char *dream;
    int hours = 12;

    snoopy = snoopy_new("Snoopy", 3, "beagle", "flying his red baron");
    if (!snoopy) {
        fprintf(stderr, "failed to create snoopy\n");
        return -1;
    }

    animal = &snoopy->dog.animal;

    printf("%s is a %d year old %s dreaming of %s, with %d flights.\n",
           *(char **)ooc_get(snoopy, "name"),
           *(int *)ooc_get(snoopy, "age"),
           *(char **)ooc_get(snoopy, "breed"),
           *(char **)ooc_get(snoopy, "imagination"),
           snoopy_flights(snoopy));

    /* The owned field again, this time one declared by a Dog subclass. */
    dream = copy_of("a chicken dinner");
    if (!dream || ooc_set(snoopy, "imagination", &dream) != 0) {
        fprintf(stderr, "failed to set imagination\n");
        free(dream);
        goto fail;
    }

    printf("%s is now dreaming of %s.\n", *(char **)ooc_get(snoopy, "name"),
           *(char **)ooc_get(snoopy, "imagination"));

    /*
     * Two underscores, so there is no way in by name in either direction. The
     * class reports the value itself instead, which is the whole point of hiding
     * it: the caller is told what it may know and nothing more.
     */
    printf("__flights is readable by name: %s, writable: %s\n",
           ooc_get(snoopy, "__flights") ? "yes" : "no",
           ooc_set(snoopy, "__flights", &hours) == 0 ? "yes" : "no");
    printf("and the class still reports %d flights.\n",
           snoopy_flights(snoopy));

    printf("a snoopy is a Dog: %s, a Cat: %s, a Garfield: %s\n",
           ooc_is_a(snoopy, &Dog_class) ? "yes" : "no",
           ooc_is_a(snoopy, &Cat_class) ? "yes" : "no",
           ooc_is_a(snoopy, &Garfield_class) ? "yes" : "no");

    animal_speak(animal);

    ooc_release(snoopy);

    return 0;

fail:
    ooc_release(snoopy);
    return -1;
}

/*
 * The hierarchy walk: four objects, one pointer type, no static information.
 *
 * This is what the runtime is for. Each object is created as its own class and
 * then handled only as an Animal *, with the pointer's type carrying nothing at
 * all about what it points to. The single animal_speak() call below reaches four
 * different implementations, and the ooc_is_a() answers come from the objects
 * themselves rather than from the call site.
 *
 * The order is the hierarchy: a root, then a class on each branch, then the two
 * subclasses of a subclass. Reading the output top to bottom shows what a
 * three-level chain does to a call that knows none of it.
 *
 * Returns 0 when the walk completes.
 */
static int visit_hierarchy(void)
{
    Animal *rex;
    Animal *mia;
    Animal *garfield;
    Animal *snoopy;

    printf("\n-- one pointer type, four runtime types --\n");

    /* Each object is created as its own class and only used as an Animal. */
    rex = (Animal *)dog_new("Rex", 5, "German Shepherd");
    mia = (Animal *)cat_new("Mia", 3, "tabby");
    garfield = (Animal *)garfield_new("Garfield", 4, "orange", "lasagna");
    snoopy = (Animal *)snoopy_new("Snoopy", 3, "beagle", "his red baron");

    if (!rex || !mia || !garfield || !snoopy) {
        fprintf(stderr, "failed to create the hierarchy\n");

        /* Release whatever did get built, since each reference is its own. */
        ooc_release(rex);
        ooc_release(mia);
        ooc_release(garfield);
        ooc_release(snoopy);

        return -1;
    }

    /*
     * Four calls to one function. Which speak() runs is decided by the vtable
     * each object carries, and the pointer type plays no part in it -- the whole
     * of the polymorphism is visible right here in the output.
     */
    animal_speak(rex);
    animal_speak(mia);
    animal_speak(garfield);
    animal_speak(snoopy);

    /*
     * And the runtime type can be recovered from the object alone. This is the
     * question a caller with an Animal * and no static information has to ask,
     * and the chain walk answers it for a base class as readily as for the exact
     * type -- which is the direction that is actually useful.
     */
    printf("\n%s is a Dog: %d, a Cat: %d, an Animal: %d\n",
           *(char **)ooc_get(rex, "name"),
           ooc_is_a(rex, &Dog_class),
           ooc_is_a(rex, &Cat_class),
           ooc_is_a(rex, &Animal_class));

    printf("%s is a Dog: %d, a Cat: %d, a Garfield: %d\n",
           *(char **)ooc_get(garfield, "name"),
           ooc_is_a(garfield, &Dog_class),
           ooc_is_a(garfield, &Cat_class),
           ooc_is_a(garfield, &Garfield_class));

    printf("%s is a Snoopy: %d, a Dog: %d, a Cat: %d\n",
           *(char **)ooc_get(snoopy, "name"),
           ooc_is_a(snoopy, &Snoopy_class),
           ooc_is_a(snoopy, &Dog_class),
           ooc_is_a(snoopy, &Cat_class));

    /*
     * A field declared on one branch is not reachable from the other. Both
     * names are absent from both chains, which is why both calls return NULL and
     * why asking for the wrong branch's field is safe rather than corrupting
     * memory at a bogus offset.
     */
    printf("\n%s has an imagination: %s, and a colour: %s\n",
           *(char **)ooc_get(snoopy, "name"),
           ooc_get(snoopy, "imagination") ? "yes" : "no",
           ooc_get(snoopy, "colour") ? "yes" : "no");

    printf("%s has a favourite food: %s, and a breed: %s\n",
           *(char **)ooc_get(garfield, "name"),
           ooc_get(garfield, "favourite_food") ? "yes" : "no",
           ooc_get(garfield, "breed") ? "yes" : "no");

    /* One reference each, so four releases. */
    ooc_release(rex);
    ooc_release(mia);
    ooc_release(garfield);
    ooc_release(snoopy);

    return 0;
}

/*
 * Run the walks in order, stopping at the first failure.
 *
 * Each walk takes one class through the whole life cycle and returns 0 on
 * success or -1 if a step the example relies on was refused, which is enough to
 * make a broken expectation visible without aborting. The short-circuit keeps the
 * output of a failing run readable: no later walk can be trusted once an earlier
 * one has misbehaved.
 *
 * The exit status is nonzero if any walk failed, so this doubles as the test the
 * `test` target runs. Returns 0 when everything succeeded.
 */
int main(void)
{
    int status = visit_dog();

    if (status == 0)
        status = visit_cat();

    if (status == 0)
        status = visit_garfield();

    if (status == 0)
        status = visit_snoopy();

    if (status == 0)
        status = visit_hierarchy();

    return status != 0;
}
