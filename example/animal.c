/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/*
 * Animal -- the base class of the example.
 *
 * It owns a copy of its name, hands ownership of every instance to the
 * caller, and forwards `speak` to the vtable so that subclasses can override
 * the behaviour.
 */

#include "animal.h"

#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Copy `text` onto the heap; the caller owns the result.
 *
 * A class that holds a string allocates it once here and hands the copy to the
 * field, which is why the destructor has something to free. The copy is
 * independent of `text`, so a name built from a stack buffer or a literal
 * outlives the call.
 *
 * Returns NULL if `text` is NULL or the allocation fails, which the callers
 * here treat as a failed construction. The terminator is copied along, so the
 * result is a C string and nothing has to be added to it.
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
 * Virtual destructor: releases what the constructor allocated.
 *
 * ooc_release() calls this with the object's storage still intact and frees the
 * object itself afterwards, so the members are readable here and only the
 * members need releasing. `name` is the one allocation Animal owns, and the
 * "name" field is marked as owned, so ooc_set() frees an old string when it is
 * replaced and the value that is left is freed here.
 *
 * The cast is free because the ooc_object header is the first member of Animal,
 * which is what makes the address a valid Animal * in the first place. A
 * subclass overrides this through its own `destroy`, and then it is also
 * responsible for the members it added.
 */
static void destroy(ooc_object *object)
{
    Animal *animal = (Animal *)object;

    free(animal->name);
}

/*
 * Default implementation of AnimalVTable::speak.
 *
 * Animal is the base class, so this is what a subclass that does not care to
 * override gets: it prints the name, which is why the name has to be a real
 * string rather than a pointer into somewhere else.
 *
 * The parameter is an Animal * rather than the runtime type, so an override
 * casts it back to its own class -- see speak() in dog.c.
 */
static void speak(Animal *animal)
{
    printf("%s makes a sound.\n", animal->name ? animal->name : "(unnamed)");
}

/*
 * Animal's own vtable.
 *
 * One table per class, shared by every instance, which is why the field in the
 * struct is a pointer to a const table rather than the table itself. A subclass
 * installs its own, so an instance of the derived class reaches the derived
 * `speak` through the same call.
 */
static const AnimalVTable vt = {
    .speak = speak,
};

/*
 * The fields ooc_get() and ooc_set() may reach by name.
 *
 * offsetof() and sizeof() keep the table in step with the struct, so a changed
 * member type or position needs no edit here. The last column marks a field
 * the class owns: `name` is a string on the heap, so ooc_set() releases the
 * old one when a new name is assigned, while `age` is copied as it stands.
 *
 * The two underscore names are published so the library can enforce what they
 * mean. "_id" is readable and refuses to be written, and "__legs" is readable
 * by neither, which is why the class hands it out through animal_legs().
 */
static const ooc_field Animal_fields[] = {
    { "name",   offsetof(Animal, name),   sizeof(((Animal *)0)->name),   1 },
    { "age",    offsetof(Animal, age),    sizeof(((Animal *)0)->age),    0 },
    { "_id",    offsetof(Animal, _id),    sizeof(((Animal *)0)->_id),    0 },
    { "__legs", offsetof(Animal, __legs), sizeof(((Animal *)0)->__legs), 0 },
    { NULL,     0,                        0,                            0 },
};

/*
 * Animal's runtime type record.
 *
 * `size` is sizeof(Animal) and not the size of a base, because ooc_new()
 * allocates exactly this much and the derived classes that embed Animal need
 * room for their own members on top. `destroy` is the destructor above,
 * `super` is NULL because Animal is a root class, and `fields` is the table
 * that gives ooc_get() and ooc_set() something to resolve.
 *
 * The record is a file-scope constant, which is what ooc_new() writes into
 * every object it allocates.
 */
const ooc_class Animal_class = {
    .size    = sizeof(Animal),
    .destroy = destroy,
    .super   = NULL,
    .fields  = Animal_fields,
};

/*
 * Hand out the next serial number.
 *
 * File-scope so the numbers keep climbing across calls, and private to this
 * file so no other class can hand them out or reset them. It is the only thing
 * that decides what `_id` becomes.
 */
static int next_id = 1;

/*
 * Initialise the members Animal owns.
 *
 * Separate from animal_new() so a subclass constructor can build the base part
 * instead of duplicating it: dog_new() calls this and then installs its own
 * vtable and its own members. The `vtable` it sets is Animal's, which a
 * subclass is expected to overwrite.
 *
 * The caller supplies storage that is already an object, ooc_new() having
 * allocated it, so there is nothing here to release: the name is copied into a
 * field the object now owns and the destructor frees it. The two underscore
 * fields are written here and nowhere else, which is what keeps them to the
 * class. The field table publishes both so the library knows where they are,
 * and the leading underscores tell ooc_set() and ooc_get() to leave them alone.
 *
 * Requires zero-initialised members and externally synchronized constructors.
 * Returns -1 for NULL, repeated initialisation, exhausted IDs or a failed name
 * copy. Failure leaves the object unchanged and still owned by the caller.
 */
int animal_init(Animal *animal, const char *name, int age)
{
    char *copy;

    if (!animal || animal->vtable || animal->name || next_id == 0)
        return -1;

    copy = dupstr(name);

    if (!copy)
        return -1;

    animal->vtable  = &vt;
    animal->name    = copy;
    animal->age     = age;
    animal->_id     = next_id;
    next_id = next_id == INT_MAX ? 0 : next_id + 1;
    animal->__legs  = 4;

    return 0;
}

/*
 * Read back `_id`, the field a caller may also reach with ooc_get().
 *
 * An accessor alongside the by-name route rather than instead of it, and the
 * difference is the const: this one does not require a writable object, so it
 * can be called on a const Animal *.
 *
 * Returns 0 for a NULL object, which is indistinguishable from a real id of
 * zero, so a caller that has to tell the two apart should check the pointer
 * first. Ids start at 1.
 */
int animal_id(const Animal *animal)
{
    return animal ? animal->_id : 0;
}

/*
 * Read back `__legs`, the field nothing outside the class can reach by name.
 *
 * ooc_get(dog, "__legs") returns NULL, so this accessor is the only way to
 * learn the value -- which is the whole point of a two-underscore name. The
 * class decides what a caller is told, and could as well return something
 * derived from `__legs` instead of the field itself.
 *
 * Returns 0 for a NULL object, as above.
 */
int animal_legs(const Animal *animal)
{
    return animal ? animal->__legs : 0;
}

/*
 * Create an Animal named `name` and return it, or NULL.
 *
 * The object is allocated through ooc_new() and comes back with a reference
 * count of one, so the caller owns it and must pass it to ooc_release()
 * eventually. animal_init() fills in the members, and a failure there releases
 * the half-built object, since the destructor is already in place and finds
 * `name` still NULL to free.
 *
 * This is the constructor a caller normally uses; animal_init() is what a
 * subclass uses instead, because it is building the base part of a larger
 * object rather than a whole one.
 *
 * Returns NULL if the allocation fails or the name could not be copied. In
 * both cases nothing is left to release.
 */
Animal *animal_new(const char *name, int age)
{
    Animal *animal = ooc_new(&Animal_class);

    if (!animal)
        return NULL;

    if (animal_init(animal, name, age) != 0) {
        ooc_release(animal);
        return NULL;
    }

    return animal;
}

/*
 * Dispatch `speak` through the vtable.
 *
 * The call is virtual: which implementation runs is decided by the vtable the
 * object carries, not by the static type of the pointer handed in. A Dog and an
 * Animal pointer to the same object therefore print different things.
 *
 * The three checks are what keep the call safe on a partial object. A NULL
 * animal is skipped, a NULL vtable means the object was never initialised, and
 * a NULL `speak` means a vtable that does not implement the method. All three
 * are tolerated silently, since a class is not obliged to implement anything.
 */
void animal_speak(Animal *animal)
{
    if (animal && animal->vtable && animal->vtable->speak)
        animal->vtable->speak(animal);
}
