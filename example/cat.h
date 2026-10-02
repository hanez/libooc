/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2.0
 */

#ifndef CAT_H
#define CAT_H

#include "animal.h"

typedef struct Cat Cat;

/*
 * Derived class.
 *
 * The embedded Animal comes first, so a Cat pointer can be handed to any
 * function expecting an Animal, and Cat adds a colour and a private `_lives` of
 * its own.
 *
 * Two things follow from that ordering. The ooc_object header inside Animal
 * stays at offset zero, where the library looks for it, and the upcast is a
 * no-op cast because both addresses are the same -- which is what lets speak()
 * in cat.c cast its Animal * back to a Cat * without arithmetic. The cost is
 * that a Cat may not add a member of its own before the base.
 *
 * The struct holds no destructor of its own; the one Cat registers lives in
 * Cat_class and releases both the colour and Animal's name.
 */
struct Cat {
    Animal animal;
    char *colour;

    /*
     * Private to Cat, and readable by name: one leading underscore stops
     * ooc_set() from writing it but leaves ooc_get() alone. Animal's `_id` works
     * the same way, so the rule is the one in the library rather than something
     * the subclass has to repeat.
     */
    int _lives;
};

/*
 * Cat's runtime type record, the value ooc_new() takes.
 *
 * Its `super` is Animal_class, so a Cat inherits Animal's fields, and its
 * `size` is sizeof(Cat), so the allocation has room for the colour and the
 * lives count.
 */
extern const ooc_class Cat_class;

/*
 * Create a Cat named `name` of the given `colour` and return it, or NULL.
 *
 * The result has a reference count of one, so ownership is the caller's and it
 * must reach ooc_release() eventually. Returns NULL if any step of the
 * construction fails, leaving nothing to release.
 *
 * The object is a Cat and answers to Cat's `speak`, whether it is later reached
 * through a Cat * or an Animal *.
 */
Cat *cat_new(const char *name, int age, const char *colour);

/*
 * Initialise the members Cat owns, so a subclass constructor can build the Cat
 * part of a larger object instead of duplicating it.
 *
 * This is the second link in the chain of constructors below Animal: a subclass
 * of Cat calls this, gets Cat's vtable installed, and then overwrites the
 * vtable with its own the same way cat_new() does.
 *
 * The caller supplies storage that ooc_new() has already allocated and
 * zeroed. On failure the object is left exactly as it was found, so the caller
 * may release it without a destructor having anything to do.
 *
 * Returns 0, or -1 for a NULL cat, an already initialised one, or a failed copy
 * of the colour. Requires zero-initialised members and external synchronization
 * between constructors in different threads.
 */
int cat_init(Cat *cat, const char *name, int age, const char *colour);

#endif /* CAT_H */
