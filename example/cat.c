/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/*
 * Cat -- a second Animal subclass, alongside Dog.
 *
 * The point of having two is that nothing here is special to Dog: the same three
 * steps make any subclass, and a Cat is a different runtime type with its own
 * vtable and its own destructor. A caller holding an Animal * for a Cat and one
 * for a Dog gets different behaviour out of the same call, which is the whole
 * argument for the vtable.
 *
 * Where Cat differs from Dog is in its members. `colour` is another owned string,
 * while `_lives` is an int marked private by its leading underscore, so the
 * class owns it and ooc_set() turns a write away even though the caller can
 * still read it by name.
 */

#include "cat.h"

#include <stddef.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Copy `text` onto the heap; the caller owns the result.
 *
 * A private copy of Animal's helper, and identical to it. Each class keeps its
 * own: exposing dupstr() would mean every subclass reaching into a base class'
 * internals to build its members, which is the coupling subclassing is meant to
 * remove. The size is checked before the terminator is added, so a string long
 * enough to wrap cannot ask malloc() for a short buffer.
 *
 * Returns NULL if `text` is NULL or the allocation fails.
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
 * Virtual destructor: releases everything cat_new() allocated.
 *
 * This replaces Animal's destructor rather than running after it, because the
 * library calls the one belonging to the object's runtime type and stops there.
 * So a Cat is responsible for Animal's members too: `name` is freed below even
 * though animal_init() allocated it, and Animal's own destroy() never runs for
 * a Cat. The second free is the half of a derived destructor that is easy to
 * forget.
 *
 * `_lives` needs no freeing, being an int, which is worth noticing: what a
 * destructor releases is the allocations a constructor made, not the members.
 *
 * The order of the two frees does not matter, since the allocations are
 * independent, and both are owned: "colour" is marked as owned in the field
 * table and "name" in Animal's, so ooc_set() releases a string when it replaces
 * one and whatever is left is freed here.
 */
static void destroy(ooc_object *object)
{
    Cat *cat = (Cat *)object;

    free(cat->colour);
    free(cat->animal.name);
}

/*
 * Cat's implementation of AnimalVTable::speak.
 *
 * The signature is Animal's, not Cat's: a vtable entry is called through the
 * base class' type, so the implementation casts its parameter back down. That
 * cast is safe only because Animal is the first member of Cat, which makes the
 * two addresses the same.
 *
 * The members are read through the cast, including the ones Animal owns, so
 * `name` and `_lives` are as available here as they are in Animal's own speak().
 * The fallbacks keep a cleared string from reaching printf(), since a field set
 * to NULL is a legitimate state -- ooc_set(dog, "name", NULL) is allowed to
 * succeed -- and a vtable entry is called with whatever the object currently
 * holds.
 */
static void speak(Animal *animal)
{
    Cat *cat = (Cat *)animal;

    printf("%s says: Meow! (%s, %d lives left)\n",
           cat->animal.name ? cat->animal.name : "(unnamed)",
           cat->colour ? cat->colour : "(unknown colour)",
           cat->_lives);
}

/*
 * Cat's own vtable, identical in shape to Animal's and holding Cat's speak.
 *
 * A second table with the same layout as Animal's, which is what a vtable buys:
 * the slot is chosen by the class that owns the table, not by the type of the
 * pointer the caller happens to have. Cat and Dog both override `speak`, and
 * neither can tell the other about it.
 */
static const AnimalVTable vt = {
    .speak = speak,
};

/*
 * The fields Cat adds on top of the ones Animal publishes.
 *
 * A derived class lists only what it declares itself; the rest is inherited
 * rather than repeated. Anything missing here -- "name", "age", "_id" and
 * "__legs" -- is still reachable through ooc_get() and ooc_set(), because
 * Cat_class names Animal_class as its base and lookup walks the chain.
 *
 * `colour` is marked as owned, exactly as Dog's breed is, so replacing it
 * through ooc_set() releases the string it held before. `_lives` carries a
 * leading underscore, so ooc_set() refuses to write it while ooc_get() still
 * returns it: the underscore rule works the same on a field the subclass
 * declared as on one the base declared.
 */
static const ooc_field Cat_fields[] = {
    { "colour", offsetof(Cat, colour), sizeof(((Cat *)0)->colour), 1 },
    { "_lives", offsetof(Cat, _lives), sizeof(((Cat *)0)->_lives), 0 },
    { NULL,     0,                    0,                          0 },
};

/*
 * Cat's runtime type record.
 *
 * `super` is what makes Cat an Animal: field lookup continues from
 * Animal_class, so "name", "age", "_id" and "__legs" resolve even though Cat
 * does not list them. `size` is sizeof(Cat) rather than sizeof(Animal), since
 * the allocation has to hold the colour and the lives count as well.
 *
 * The record is a file-scope constant, as it is for every class, and ooc_new()
 * writes its address into each object it allocates -- which is how a Cat and a
 * Dog stay distinguishable while both are passed around as Animal.
 */
const ooc_class Cat_class = {
    .size = sizeof(Cat),
    .destroy = destroy,
    .super = &Animal_class,
    .fields = Cat_fields,
};

/*
 * Create a Cat named `name` of the given `colour` and return it, or NULL.
 *
 * Three steps, in the order a subclass constructor needs them. The object is
 * allocated through ooc_new() with a reference count of one, so the caller owns
 * it and must release it. animal_init() fills in the base part, including the
 * `_id` and `__legs` that only Animal may write. Then the vtable is replaced
 * with Cat's, so the object answers to Cat's speak rather than Animal's.
 *
 * The vtable is overwritten after animal_init() rather than before, since that
 * call installs Animal's table and this one has to win.
 *
 * `_lives` is set here and nowhere else. A caller can read it with ooc_get() and
 * cannot write it with ooc_set(), so this constructor and any future method of
 * Cat are the only places the value can change -- which is the point of marking
 * it with an underscore.
 *
 * Each step can fail, and both failures release the object, which is safe
 * because the destructor is already registered and frees whatever is present:
 * after a failed animal_init() there is no name and no colour to free, and after
 * a failed colour copy the name has to be freed, which is exactly what the
 * destructor does. Returns NULL if any step fails, leaving nothing to release.
 */
Cat *cat_new(const char *name, int age, const char *colour)
{
    Cat *cat = ooc_new(&Cat_class);

    if (!cat)
        return NULL;

    /*
     * Let Animal build the part it owns, including the private `_id`, then
     * take over the vtable with Cat's own.
     */
    if (animal_init(&cat->animal, name, age) != 0) {
        ooc_release(cat);
        return NULL;
    }

    cat->animal.vtable = &vt;
    cat->colour = dupstr(colour);
    cat->_lives = 9;

    if (!cat->colour) {
        ooc_release(cat);
        return NULL;
    }

    return cat;
}
