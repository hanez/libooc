/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/*
 * Snoopy -- a subclass of Dog, and so a subclass of a subclass.
 *
 * The parallel of garfield.c on the other branch of the hierarchy, and the two
 * together are what make the hierarchy worth having: Garfield is a Cat,
 * Snoopy is a Dog, both are Animals, and a caller holding an Animal * can ask
 * ooc_is_a() which one it has and get an answer.
 *
 * Like every subclass here, Snoopy allocates from its own class record, builds
 * the classes above it by calling their constructors rather than duplicating
 * them, and registers its own vtable and destructor. What this branch adds is
 * the fork: `imagination` and `__flights` exist only on Snoopy, and the chain
 * from Snoopy_class leads to Dog_class rather than to Cat_class, so a name
 * declared on one branch is unreachable from the other.
 */

#include "snoopy.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Copy `text` onto the heap; the caller owns the result.
 *
 * A private copy of the same helper animal.c, cat.c, dog.c and garfield.c each
 * keep, and identical to all of them. Four copies is past the point where a real
 * codebase would publish it instead, but each class owning its own construction
 * state is the arrangement the rest of these examples follow, and a divergence
 * here would be the thing to notice rather than copy.
 *
 * Returns NULL if `text` is NULL or the allocation fails. The size is checked
 * before the terminator is added, so a string long enough to wrap cannot ask
 * malloc() for a short buffer.
 */
static char *dupstr(const char *text)
{
    size_t len;
    char *copy;

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
 * Virtual destructor: releases everything snoopy_new() allocated.
 *
 * The library calls the destructor belonging to the object's runtime type and
 * stops there, so this one owns the whole hierarchy. Three frees where Dog
 * needed two: `imagination` is Snoopy's, `breed` came from dog_init() and
 * `name` from animal_init() before that, and neither Dog's destroy() nor
 * Animal's ever runs for a Snoopy.
 *
 * The branch makes no difference to this. A destructor walks the struct
 * downwards through whatever it was given, and the only thing that has to be
 * right is that it frees each owned allocation exactly once.
 *
 * All three fields are marked as owned in their own class' table, so ooc_set()
 * has already released a previous value where there was one and what is left is
 * freed here.
 */
static void destroy(ooc_object *object)
{
    Snoopy *snoopy = (Snoopy *)object;

    free(snoopy->imagination);
    free(snoopy->dog.breed);
    free(snoopy->dog.animal.name);
}

/*
 * Snoopy's implementation of AnimalVTable::speak.
 *
 * The signature is Animal's, not Snoopy's, and the cast back down is the same
 * no-op at every level, since Animal sits at offset zero however many classes
 * are embedded above it.
 *
 * `__flights` is read directly here, from the struct definition where the member
 * is in view. That is the point of a two-underscore name: it keeps callers out
 * through the name-based API while leaving the class itself free to use the
 * field however it likes. An accessor is what a caller would be given instead.
 *
 * The fallbacks keep a cleared string from reaching printf(), since a field set
 * to NULL is a legitimate state and a vtable entry is called with whatever the
 * object currently holds.
 */
static void speak(Animal *animal)
{
    Snoopy *snoopy = (Snoopy *)animal;

    printf("%s says: Woof! (%s, dreaming of %s, %d flights so far)\n",
           snoopy->dog.animal.name ? snoopy->dog.animal.name : "(unnamed)",
           snoopy->dog.breed ? snoopy->dog.breed : "(unknown breed)",
           snoopy->imagination ? snoopy->imagination : "(nothing much)",
           snoopy->__flights);
}

/*
 * Snoopy's own vtable, holding Snoopy's speak.
 *
 * Identical in shape to the tables in animal.c, dog.c and cat.c. Snoopy and Dog
 * both declare `speak` and neither can tell the other about it: the slot is
 * chosen by the class that owns the table, and a Snoopy reaches this one.
 */
static const AnimalVTable vt = {
    .speak = speak,
};

/*
 * The fields Snoopy adds on top of the ones above it.
 *
 * A derived class lists only what it declares itself. Everything else -- "breed"
 * from Dog, then "name", "age", "_id" and "__legs" from Animal -- resolves by
 * walking up from Snoopy_class through Dog_class to Animal_class.
 *
 * `imagination` is marked as owned, exactly as Dog's breed is, so replacing it
 * through ooc_set() releases the string it held before. `__flights` carries two
 * underscores, so ooc_get() withholds it as well as ooc_set() refusing to write
 * it, leaving snoopy_flights() as the only route to the value.
 *
 * The branch is what stops a name leaking sideways: a Snoopy has no "colour"
 * and a Garfield has no "imagination", because neither chain contains the class
 * that would declare it.
 */
static const ooc_field Snoopy_fields[] = {
    { "imagination", offsetof(Snoopy, imagination),
      sizeof(((Snoopy *)0)->imagination), 1 },
    { "__flights",   offsetof(Snoopy, __flights),
      sizeof(((Snoopy *)0)->__flights),   0 },
    { NULL,          0, 0,                          0 },
};

/*
 * Snoopy's runtime type record.
 *
 * `super` is Dog_class, and that pointer is what puts Snoopy on the Dog branch:
 * ooc_new() writes this record's address into every object it allocates, and
 * field lookup, ooc_set() and ooc_is_a() all start there and walk outwards,
 * reaching Dog's and Animal's fields through it.
 *
 * `size` is sizeof(Snoopy), which must hold Dog and Snoopy's own members. The
 * runtime rejects a class whose base is larger, since the base's fields would
 * then be written past the end of the allocation.
 *
 * The record is a file-scope constant, as it is for every class.
 */
const ooc_class Snoopy_class = {
    .size    = sizeof(Snoopy),
    .destroy = destroy,
    .super   = &Dog_class,
    .fields  = Snoopy_fields,
};

/*
 * Initialise the members Snoopy owns, leaving the object ready to speak as a
 * Snoopy.
 *
 * The third link in the chain of constructors, composed the same way
 * garfield_init() is: dog_init() builds the Dog part, which calls animal_init()
 * for the Animal part, and Snoopy overwrites the vtable afterwards since
 * dog_init() installs Dog's table.
 *
 * The imagination is copied first, so a failure here cannot leave a partly built
 * object behind and the caller may release the zeroed storage without the
 * destructor having anything to do.
 *
 * `__flights` starts at zero and is written here and nowhere else, which is what
 * makes the two-underscore rule meaningful: the class decides when it changes,
 * and no caller can reach it by name to change it sooner.
 *
 * Returns 0, or -1 for a NULL object, an already initialised one, or a failed
 * copy. Requires zero-initialised members and external synchronization between
 * constructors in different threads.
 */
int snoopy_init(Snoopy *snoopy, const char *name, int age, const char *breed,
                const char *imagination)
{
    char *copy;

    if (!snoopy || snoopy->imagination || snoopy->dog.breed ||
        snoopy->dog.animal.vtable)
        return -1;

    copy = dupstr(imagination);

    if (!copy)
        return -1;

    /*
     * Let Dog build the part it owns -- which lets Animal build the part it
     * owns -- then take over the vtable with Snoopy's own.
     */
    if (dog_init(&snoopy->dog, name, age, breed) != 0) {
        free(copy);
        return -1;
    }

    snoopy->imagination = copy;
    snoopy->dog.animal.vtable = &vt;

    return 0;
}

/*
 * Create a Snoopy named `name`, of the given `breed` and `imagination`, and
 * return it, or NULL.
 *
 * Two steps, as in dog_new() and cat_new(): allocate through ooc_new() with a
 * reference count of one, so the caller owns the result and must release it, and
 * then let snoopy_init() build the members. The allocation carries Snoopy's
 * destructor from the start, which frees the whole hierarchy, so a failed init
 * releases cleanly -- on every failure path above, nothing was written and
 * free(NULL) is what the destructor finds.
 *
 * Returns NULL if the allocation fails or snoopy_init() refuses, in both cases
 * leaving nothing to release.
 */
Snoopy *snoopy_new(const char *name, int age, const char *breed,
                   const char *imagination)
{
    Snoopy *snoopy = ooc_new(&Snoopy_class);

    if (!snoopy)
        return NULL;

    if (snoopy_init(snoopy, name, age, breed, imagination) != 0) {
        ooc_release(snoopy);
        return NULL;
    }

    return snoopy;
}

/*
 * Read back `__flights`, the field ooc_get() withholds.
 *
 * The only way to learn the value, since nothing outside the class can reach the
 * field by name in either direction. Animal exposes `__legs` the same way, and
 * the two differ from a single underscore only in that this one cannot be read
 * by name at all.
 *
 * Takes a const object, so a caller with a const pointer can still be told the
 * count.
 *
 * Returns 0 for a NULL object, which is indistinguishable from a real count of
 * zero, so a caller that has to tell the two apart should check the pointer
 * first.
 */
int snoopy_flights(const Snoopy *snoopy)
{
    return snoopy ? snoopy->__flights : 0;
}