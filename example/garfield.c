/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/*
 * Garfield -- a subclass of Cat, and so a subclass of a subclass.
 *
 * Everything a first-level subclass does, this file does again one level down:
 * the object is allocated from Garfield_class, whose `super` is Cat_class so
 * field lookup continues through Cat to Animal; the members of the classes above
 * are built by cat_init() rather than duplicated; and a separate vtable and
 * destructor are registered for Garfield, so the override and the extra
 * allocation are cleaned up by the object's runtime type.
 *
 * Adding a level turns out to change none of the rules, which is the point worth
 * making with an example. Ownership, the underscore conventions, the virtual
 * destructor and the chain walk all behave identically whether the base is two
 * levels up or one. What does change is the destructor, and that is the part
 * worth reading closely: it now frees three strings instead of two, and forgets
 * the middle one at its peril.
 */

#include "garfield.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Copy `text` onto the heap; the caller owns the result.
 *
 * A private copy of the same helper animal.c, cat.c and dog.c each keep, and
 * identical to all of them. By now the duplication is the point being made
 * rather than a detail worth hiding: four identical copies is where a real
 * codebase would stop and either publish the helper or leave it to the class
 * that owns it. The library is too small to have an opinion, and an example is
 * clearer for showing what each class is actually responsible for.
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
 * Virtual destructor: releases everything garfield_new() allocated.
 *
 * The library calls the destructor belonging to the object's runtime type and
 * stops there, so this one owns the whole hierarchy and not merely Garfield's
 * own member. Three frees are needed where Cat needed two: `favourite_food` is
 * Garfield's, `colour` was allocated by cat_init() and `name` by animal_init()
 * before that, and neither Cat's destroy() nor Animal's ever runs for a
 * Garfield.
 *
 * Omitting the middle free is the mistake a third level invites, and it is
 * silent: the object still looks fine, and the leak only shows up in a memory
 * checker. Forgetting `name` on the way down would have the same effect one
 * level earlier.
 *
 * The order does not matter, since the three allocations are independent, and
 * all three fields are marked as owned in their own class' table -- so ooc_set()
 * has already released a previous value where there was one, and what remains
 * here is freed on destruction.
 */
static void destroy(ooc_object *object)
{
    Garfield *garfield = (Garfield *)object;

    free(garfield->favourite_food);
    free(garfield->cat.colour);
    free(garfield->cat.animal.name);
}

/*
 * Garfield's implementation of AnimalVTable::speak.
 *
 * The signature is Animal's, not Garfield's, and the cast back down is the same
 * no-op at every level, since Animal sits at offset zero however many classes
 * are embedded above it. That is what makes a deep hierarchy cheap: no
 * adjustment, no pointer arithmetic, and the cast is valid because the object
 * really was a Garfield.
 *
 * The members come from three different classes in one printf() -- the name from
 * Animal, the colour and the lives count from Cat, the food and the meals from
 * Garfield -- which is the clearest statement of what inheritance bought: an
 * override with access to the whole hierarchy behind it.
 *
 * The fallbacks keep a cleared string from reaching printf(). A field set to
 * NULL is a legitimate state, ooc_set(garfield, "favourite_food", NULL) being
 * allowed to succeed, and a vtable entry is called with whatever the object
 * currently holds.
 */
static void speak(Animal *animal)
{
    Garfield *garfield = (Garfield *)animal;

    printf("%s says: Meow! (%s, %s, meal %d, %d lives left)\n",
           garfield->cat.animal.name ? garfield->cat.animal.name : "(unnamed)",
           garfield->cat.colour ? garfield->cat.colour : "(unknown colour)",
           garfield->favourite_food ? garfield->favourite_food
                                    : "(no favourite food)",
           garfield->_meals,
           garfield->cat._lives);
}

/*
 * Garfield's own vtable, holding Garfield's speak.
 *
 * Identical in shape to the tables in animal.c, cat.c and dog.c. A vtable slot
 * is chosen by the class that owns the table, so a Garfield reaches this speak
 * and never Cat's, even though Cat declared the same method -- the override
 * holds all the way down.
 */
static const AnimalVTable vt = {
    .speak = speak,
};

/*
 * The one field Garfield adds on top of the ones above it.
 *
 * A derived class lists only what it declares itself. Everything else --
 * "colour" and "_lives" from Cat, "name", "age", "_id" and "__legs" from Animal
 * -- resolves by walking up from Garfield_class through Cat_class to
 * Animal_class. Inserting a class in the middle hides nothing.
 *
 * `favourite_food` is marked as owned, exactly as Cat's colour and Dog's breed
 * are, so replacing it through ooc_set() releases the string it held before.
 * `_meals` carries a leading underscore, so ooc_set() refuses to write it while
 * ooc_get() still returns it, and the rule applies to a field this class
 * declared rather than one it inherited.
 */
static const ooc_field Garfield_fields[] = {
    { "favourite_food", offsetof(Garfield, favourite_food),
      sizeof(((Garfield *)0)->favourite_food), 1 },
    { "_meals",         offsetof(Garfield, _meals),
      sizeof(((Garfield *)0)->_meals),         0 },
    { NULL,             0, 0,                                   0 },
};

/*
 * Garfield's runtime type record.
 *
 * `super` is Cat_class, and that single pointer is what makes this a subclass of
 * a subclass: ooc_new() writes this record's address into every object it
 * allocates, and ooc_get(), ooc_set() and ooc_is_a() all start from there and
 * walk outwards, reaching Cat's and Animal's fields through it.
 *
 * `size` is sizeof(Garfield), which must hold Cat and the food on top. The
 * runtime rejects a class whose base is larger, since the base's fields would
 * then be written past the end of the allocation.
 *
 * The record is a file-scope constant, as it is for every class.
 */
const ooc_class Garfield_class = {
    .size    = sizeof(Garfield),
    .destroy = destroy,
    .super   = &Cat_class,
    .fields  = Garfield_fields,
};

/*
 * Initialise the members Garfield owns, leaving the object ready to speak as a
 * Garfield.
 *
 * The third link in the chain of constructors, and it is a strict composition
 * of the two below it. cat_init() builds the Cat part -- which calls
 * animal_init() for the Animal part -- and Garfield overwrites the vtable
 * afterwards, since cat_init() installs Cat's table and Garfield's has to win.
 *
 * The food is copied first, for the same reason cat_init() copies the colour
 * before calling animal_init(): a failure here cannot leave a partly built
 * object behind, so the caller may release the zeroed storage without the
 * destructor having anything to do.
 *
 * `_meals` is set here and nowhere else, so it changes only through this
 * constructor or a future method of Garfield. A caller can read it by name and
 * cannot write it, which is the contract the underscore asks for.
 *
 * Returns 0, or -1 for a NULL object, an already initialised one, or a failed
 * copy. Requires zero-initialised members and external synchronization between
 * constructors in different threads.
 */
int garfield_init(Garfield *garfield, const char *name, int age,
                  const char *colour, const char *favourite_food)
{
    char *copy;

    if (!garfield || garfield->favourite_food || garfield->cat.colour ||
        garfield->cat.animal.vtable)
        return -1;

    copy = dupstr(favourite_food);

    if (!copy)
        return -1;

    /*
     * Let Cat build the part it owns -- which lets Animal build the part it
     * owns -- then take over the vtable with Garfield's own.
     */
    if (cat_init(&garfield->cat, name, age, colour) != 0) {
        free(copy);
        return -1;
    }

    garfield->favourite_food = copy;
    garfield->cat.animal.vtable = &vt;
    garfield->_meals = 1;

    return 0;
}

/*
 * Create a Garfield named `name`, of the given `colour` and `favourite_food`,
 * and return it, or NULL.
 *
 * Two steps, as in cat_new() and dog_new(): allocate through ooc_new() with a
 * reference count of one, so the caller owns the result and must release it,
 * and then let garfield_init() build the members. The allocation carries
 * Garfield's destructor from the start, which frees the whole hierarchy, so a
 * failed init releases cleanly -- on every failure path above, nothing was
 * written and free(NULL) is what the destructor finds.
 *
 * Returns NULL if the allocation fails or garfield_init() refuses, in both cases
 * leaving nothing to release.
 */
Garfield *garfield_new(const char *name, int age, const char *colour,
                       const char *favourite_food)
{
    Garfield *garfield = ooc_new(&Garfield_class);

    if (!garfield)
        return NULL;

    if (garfield_init(garfield, name, age, colour, favourite_food) != 0) {
        ooc_release(garfield);
        return NULL;
    }

    return garfield;
}

/*
 * Read back `_meals`, the field ooc_get() also returns.
 *
 * Takes a const object, so it can be called where ooc_set() could not -- a
 * caller with a const pointer can still be told the count.
 *
 * Returns 0 for a NULL object, which is indistinguishable from a real count of
 * zero, so a caller that has to tell the two apart should check the pointer
 * first. The count starts at 1.
 */
int garfield_meals(const Garfield *garfield)
{
    return garfield ? garfield->_meals : 0;
}