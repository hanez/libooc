/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

#ifndef SNOOPY_H
#define SNOOPY_H

#include "dog.h"

typedef struct Snoopy Snoopy;

/*
 * A subclass of Dog, and so a subclass of a subclass on the other branch of the
 * example hierarchy.
 *
 * The embedded Dog comes first, which is the same arrangement Garfield uses: the
 * ooc_object header stays at offset zero where the library looks for it, and the
 * upcast to Animal * is a no-op cast because every address is the same. No member
 * may go before the base.
 *
 * Snoopy adds an imagination and keeps the flying hours to itself. The important
 * thing about this branch is not the members but the shape: the hierarchy now
 * forks, so Garfield is a Cat and Snoopy is a Dog and neither is anything to do
 * with the other beyond their shared Animal. Field lookup walks up and stops at
 * the first class that has the name, so "imagination" never resolves on a
 * Garfield and "colour" never resolves on a Snoopy.
 */
struct Snoopy {
    Dog dog;

    /*
     * Snoopy's own field, and the only public one it adds. Marked as owned in
     * the field table, so ooc_set() releases the string it displaces exactly as
     * it does for a Dog's breed. Nothing about ownership changes with depth.
     */
    char *imagination;

    /*
     * A private counter, set once and not adjustable from outside.
     *
     * Two underscores rather than one, which is a difference in degree rather
     * than in kind: `__flights` is unreadable as well as unwritable by name, so
     * snoopy_flights() is the only way to learn it. Dog has no private field of
     * its own and Animal hides `__legs` the same way, so the convention is
     * visible at every level of this hierarchy.
     */
    int __flights;
};

/*
 * Snoopy's runtime type record, the value ooc_new() takes.
 *
 * Its `super` is Dog_class, so lookup continues from there for "breed" and
 * onwards to Animal_class for "name", "age", "_id" and "__legs". Its `size` is
 * sizeof(Snoopy), which has to hold Dog and Snoopy's own members on top.
 */
extern const ooc_class Snoopy_class;

/*
 * Create a Snoopy named `name`, of the given `breed` and `imagination`, and
 * return it, or NULL.
 *
 * The result has a reference count of one, so ownership is the caller's and it
 * must reach ooc_release() eventually. Returns NULL if any step of the
 * construction fails, leaving nothing to release.
 *
 * The object is a Snoopy and answers to Snoopy's `speak` however it is later
 * reached -- through a Snoopy *, a Dog * or an Animal * alike.
 */
Snoopy *snoopy_new(const char *name, int age, const char *breed,
                   const char *imagination);

/*
 * Initialise the members Snoopy owns, so a subclass of Snoopy could build this
 * part for itself.
 *
 * The third link in the chain of constructors: snoopy_init() composes
 * dog_init(), which composes animal_init(). Each one installs its own vtable and
 * the one above overwrites it, so the object ends up speaking as the most
 * derived class.
 *
 * The caller supplies storage that ooc_new() has already allocated and zeroed.
 * On failure the object is left exactly as it was found, so the caller may
 * release it without a destructor having anything to do.
 *
 * Returns 0, or -1 for a NULL object, an already initialised one, or a failed
 * copy. Requires zero-initialised members and external synchronization between
 * constructors in different threads.
 */
int snoopy_init(Snoopy *snoopy, const char *name, int age, const char *breed,
                const char *imagination);

/*
 * Read back `__flights`, the field ooc_get() withholds.
 *
 * The only route to the value, which is what a two-underscore name asks for: no
 * amount of ooc_get() or ooc_set() will reach it, so a caller wanting the count
 * has to come through here. Animal exposes `__legs` the same way.
 *
 * Returns 0 for a NULL object, which is indistinguishable from a real count of
 * zero, so a caller that has to tell the two apart should check the pointer
 * first. The count starts at 0, since Snoopy has not flown anywhere yet.
 */
int snoopy_flights(const Snoopy *snoopy);

#endif /* SNOOPY_H */