/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

#ifndef GARFIELD_H
#define GARFIELD_H

#include "cat.h"

typedef struct Garfield Garfield;

/*
 * A subclass of a subclass, three levels below the root.
 *
 * The embedded Cat comes first, so the same two properties hold one level down
 * from the case dog.h describes: the ooc_object header stays at offset zero
 * where the library looks for it, and the upcast from Garfield * to Animal * is
 * a no-op cast because every address is the same. The cost is unchanged too --
 * no member may go before the base.
 *
 * Garfield adds a favourite food of its own and nothing that shadows Cat or
 * Animal, so every inherited field keeps the meaning it had. "colour" and
 * "_lives" still resolve to Cat's storage, and the underscore rules still apply
 * to the class that declared them: a caller can read "_lives" by name and cannot
 * write it, even though Garfield sits between the caller and the declaration.
 *
 * The struct holds no destructor of its own; the one Garfield registers lives in
 * Garfield_class and releases all three strings in the hierarchy -- the food,
 * Cat's colour and Animal's name.
 */
struct Garfield {
    Cat cat;

    /*
     * Garfield's own field, and the only one it adds. Marked as owned in the
     * field table, so ooc_set() releases the string it displaces exactly as it
     * does for a Dog's breed or a Cat's colour. A third level of subclassing
     * changes nothing about how ownership behaves.
     */
    char *favourite_food;

    /*
     * A field nobody but this class may change, counted in meals rather than
     * lives. One underscore hides it from ooc_set() and leaves it readable, and
     * garfield_meals() is the way a caller asks politely.
     *
     * Worth noting that `Cat` has a `_lives` of its own and Garfield adds a
     * second private field rather than reusing it. Private means private to the
     * declaring class, so a subclass cannot lean on the base class' private
     * state even from inside the same hierarchy -- which is why the two fields
     * coexist under different names.
     */
    int _meals;
};

/*
 * Garfield's runtime type record, the value ooc_new() takes.
 *
 * Its `super` is Cat_class rather than Animal_class, which is the whole of a
 * third level: field lookup starts here, continues at Cat_class for "colour" and
 * "_lives", and reaches Animal_class for "name", "age", "_id" and "__legs". The
 * chain is walked as far as it goes, so nothing from an ancestor is lost by
 * inserting a class in between.
 *
 * Its `size` is sizeof(Garfield), which has to hold Cat and Garfield's own
 * member on top, and the runtime rejects a class smaller than its base.
 */
extern const ooc_class Garfield_class;

/*
 * Create a Garfield named `name`, of the given `colour` and `favourite_food`,
 * and return it, or NULL.
 *
 * The result has a reference count of one, so ownership is the caller's and it
 * must reach ooc_release() eventually. Returns NULL if any step of the
 * construction fails, leaving nothing to release.
 *
 * The object is a Garfield and answers to Garfield's `speak` however it is later
 * reached -- through a Garfield *, a Cat * or an Animal * alike.
 */
Garfield *garfield_new(const char *name, int age, const char *colour,
                       const char *favourite_food);

/*
 * Initialise the members Garfield owns, so a subclass of Garfield could build
 * this part for itself.
 *
 * The third link in the chain of constructors: garfield_init() composes
 * cat_init(), which composes animal_init(). Each one installs its own vtable and
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
int garfield_init(Garfield *garfield, const char *name, int age,
                  const char *colour, const char *favourite_food);

/*
 * Read back `_meals`, the field ooc_get() also returns.
 *
 * The accessor a class provides for a value it keeps to itself. A single
 * underscore leaves the field readable by name, so this is a courtesy rather
 * than a necessity, unlike animal_legs() for the two-underscore `__legs`.
 *
 * Returns 0 for a NULL object, which is indistinguishable from a real count of
 * zero, so a caller that has to tell the two apart should check the pointer
 * first.
 */
int garfield_meals(const Garfield *garfield);

#endif /* GARFIELD_H */