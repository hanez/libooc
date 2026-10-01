/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

#ifndef ANIMAL_H
#define ANIMAL_H

#include <ooc/ooc.h>

typedef struct Animal Animal;
typedef struct AnimalVTable AnimalVTable;

/*
 * Virtual methods inherited by every animal.
 *
 * The vtable is a plain struct of function pointers, so dispatch is an ordinary
 * call through a pointer and needs no support from the compiler. A subclass
 * fills in its own table and installs it in the instance, which is how `speak`
 * reaches Dog's implementation when the object is only known as an Animal.
 *
 * Each pointer is called with the object as its first argument, so an
 * implementation casts it back to the class it belongs to.
 */
struct AnimalVTable {
    void (*speak)(Animal *self);
};

/*
 * Base class.
 *
 * Every instance starts with an ooc_object header, which the runtime needs
 * for reference counting and the virtual destructor. Subclasses embed this
 * struct as their first member, which makes the upcast free.
 */
struct Animal {
    ooc_object          object;
    const AnimalVTable *vtable;
    char               *name;
    int                 age;

    /*
     * Two fields that belong to the class alone, told apart by their leading
     * underscores.
     *
     * `_id` is published, so it can be read by name, and ooc_set() refuses to
     * write it. `__legs` is hidden outright: ooc_get() withholds it as well, so
     * nothing outside Animal can read or write it by name at all. Both are set
     * from the struct definition below, where the members are in view.
     */
    int                 _id;
    int                 __legs;
};

/*
 * Animal's runtime type record, the value ooc_new() takes.
 *
 * A subclass names it in its own `super`, which is how field lookup continues
 * up the chain and how Dog's objects still resolve "name" and "age".
 */
extern const ooc_class Animal_class;

/*
 * Create an Animal named `name` and return it, or NULL.
 *
 * Ownership of the result is the caller's, with a reference count of one, so it
 * has to reach ooc_release() eventually. Returns NULL if the allocation or the
 * copy of the name fails, in which case there is nothing to release.
 */
Animal *animal_new(const char *name, int age);

/*
 * Read back `_id`, the field ooc_get() also returns.
 *
 * Takes a const object, so it can be called where ooc_get() could not.
 */
int animal_id(const Animal *animal);

/*
 * Read back `__legs`, the field ooc_get() withholds.
 *
 * This accessor is the only way to learn the value, which is the point of a
 * two-underscore name. Returns 0 for a NULL object.
 */
int animal_legs(const Animal *animal);

/*
 * Initialise the members Animal owns, so a subclass constructor can build the
 * base part without duplicating it. Requires zero-initialised member storage
 * and external synchronization between constructors in different threads.
 * Returns 0, or -1 for NULL, an already initialised animal, exhausted positive
 * int IDs, or a failed name copy. Failure leaves the animal unchanged.
 */
int animal_init(Animal *animal, const char *name, int age);

/*
 * Dispatch `speak` through the vtable, so the implementation depends on the
 * object's runtime type. A NULL object, a NULL vtable and a NULL method are all
 * tolerated and do nothing.
 */
void animal_speak(Animal *animal);

#endif /* ANIMAL_H */
