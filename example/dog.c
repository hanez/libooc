/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/*
 * Dog -- an Animal subclass that overrides the virtual `speak` method.
 *
 * Because Dog embeds an Animal as its first member, a Dog can be passed
 * wherever an Animal is expected and still reach Dog's own vtable.
 *
 * Subclassing here means three things, and this file shows each one. The object
 * is allocated from Dog_class, whose `super` points at Animal_class so the
 * inherited fields resolve. Animal's members are built by animal_init(), the
 * base class' own entry point, rather than duplicated. And a separate vtable
 * and destructor are registered for Dog, so the override and the extra
 * allocation are cleaned up by the runtime type of the object.
 */

#include "dog.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Copy `text` onto the heap; the caller owns the result.
 *
 * A private copy of Animal's helper, and identical to it. It is duplicated
 * rather than shared because a class keeps its own constructor state: exposing
 * dupstr() would mean the derived class reaching into a base class' internals
 * to build its members, which is the coupling subclassing is meant to remove.
 *
 * Returns NULL if `text` is NULL or the allocation fails.
 */
static char *dupstr(const char *text)
{
    size_t  len;
    char   *copy;

    if (!text)
        return NULL;

    len = strlen(text);
    if (len == SIZE_MAX)
        return NULL;
    ++len;

    copy = malloc(len);
    if (copy)
        memcpy(copy, text, len);

    return copy;
}

/*
 * Virtual destructor: releases everything dog_new() allocated.
 *
 * This replaces Animal's destructor rather than running after it, because the
 * library calls the one belonging to the object's runtime type and stops
 * there. So a Dog is responsible for Animal's members too: `name` is freed
 * below even though animal_init() allocated it, and Animal's own destroy()
 * never runs for a Dog. The second free is the half of a derived destructor
 * that is easy to forget.
 *
 * The order does not matter, since the two allocations are independent, and
 * both are owned: "breed" is marked as owned in the field table, and "name" is
 * marked as owned in Animal's, so ooc_set() releases a string when it replaces
 * one and what is left here is freed on destruction.
 */
static void destroy(ooc_object *object)
{
    Dog *dog = (Dog *)object;

    free(dog->breed);
    free(dog->animal.name);
}

/*
 * Dog's implementation of AnimalVTable::speak.
 *
 * The signature is Animal's, not Dog's: a vtable entry is called through the
 * base class' type, so the implementation casts its parameter back down. That
 * cast is the cost of a subclass adding behaviour to an existing method, and
 * it is safe only because Animal is the first member of Dog, which makes the
 * two addresses the same.
 *
 * Adding a *new* virtual method is not possible this way, since the vtable is
 * the base class' struct: that takes an interface struct of its own, holding
 * the vtable pointer this class already has as `vtable`.
 */
static void speak(Animal *animal)
{
    Dog *dog = (Dog *)animal;

    printf("%s says: Woof! (%s)\n", dog->animal.name ? dog->animal.name : "(unnamed)",
           dog->breed ? dog->breed : "(unknown breed)");
}

/*
 * Dog's own vtable, identical in shape to Animal's and holding Dog's speak.
 *
 * A subclass that overrides nothing reuses the base table as it is, so the
 * override is what makes a separate table necessary here.
 */
static const AnimalVTable vt = {
    .speak = speak,
};

/*
 * The fields Dog adds on top of the ones Animal publishes.
 *
 * A derived class lists only what it declares itself; the rest is inherited
 * rather than repeated. Anything missing here -- "name", "age", and Animal's
 * two underscore names -- is still reachable through ooc_get() and ooc_set(),
 * because Dog_class names Animal_class as its base and lookup walks the chain.
 *
 * Like Animal's name, `breed` is marked as owned, so replacing it through
 * ooc_set() releases the string it held before. The underscores of the
 * inherited fields keep their meaning across the boundary: ooc_set(dog, "_id")
 * is still refused even though Dog did not declare it.
 */
static const ooc_field Dog_fields[] = {
    { "breed", offsetof(Dog, breed), sizeof(((Dog *)0)->breed), 1 },
    { NULL,    0,                    0,                          0 },
};

/*
 * Dog's runtime type record.
 *
 * `super` is what makes Dog a Dog: field lookup continues from Animal_class, so
 * "name", "age", "_id" and "__legs" resolve even though Dog does not list them.
 * `size` is sizeof(Dog) rather than sizeof(Animal), since the allocation has to
 * hold the breed as well as the base.
 */
const ooc_class Dog_class = {
    .size    = sizeof(Dog),
    .destroy = destroy,
    .super   = &Animal_class,
    .fields  = Dog_fields,
};

/*
 * Create a Dog named `name` of the given `breed` and return it, or NULL.
 *
 * Three steps, in the order a subclass constructor needs them. The object is
 * allocated through ooc_new() with a reference count of one, so the caller owns
 * it and must release it. animal_init() fills in the base part, including the
 * `_id` and `__legs` that only Animal may write. Then the vtable is replaced
 * with Dog's, so the object answers to Dog's speak rather than Animal's.
 *
 * The vtable is overwritten after animal_init() rather than before, since that
 * call installs Animal's table and this one has to win.
 *
 * Each step can fail, and both failures release the object, which is safe
 * because the destructor is already registered and frees whatever is present:
 * after a failed animal_init() there is no name and no breed to free, and after
 * a failed breed copy the name has to be freed, which is exactly what the
 * destructor does. Returns NULL if any step fails, leaving nothing to release.
 */
Dog *dog_new(const char *name, int age, const char *breed)
{
    Dog *dog = ooc_new(&Dog_class);

    if (!dog)
        return NULL;

    /*
     * Let Animal build the part it owns, including the private `_id`, then
     * take over the vtable with Dog's own.
     */
    if (animal_init(&dog->animal, name, age) != 0) {
        ooc_release(dog);
        return NULL;
    }

    dog->animal.vtable = &vt;
    dog->breed         = dupstr(breed);

    if (!dog->breed) {
        ooc_release(dog);
        return NULL;
    }

    return dog;
}
