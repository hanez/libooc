/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2.0
 */

/*
 * Walks through the libooc life cycle twice over, once per subclass.
 *
 * The Dog walk covers the whole API: allocation, a second reference through a
 * base class, reading and writing fields by name, the private field rules, and
 * the virtual destructor. The Cat walk then repeats the same calls against a
 * second subclass, so the output makes the one thing that is not visible in the
 * source clear: `animal_speak` is a single call site and still reaches Dog's
 * implementation for one object and Cat's for the other.
 */

#include "cat.h"
#include "dog.h"

#include <ooc/ooc.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Copy `text` onto the heap, because a field that owns its value needs heap. */
static char *copy_of(const char *text)
{
    size_t len = strlen(text);
    char *copy = malloc(len + 1);
    ++len;

    if (copy)
        memcpy(copy, text, len);

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

int main(void)
{
    int status = visit_dog();

    if (status == 0)
        status = visit_cat();

    return status != 0;
}
