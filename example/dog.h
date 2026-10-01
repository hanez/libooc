/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

#ifndef DOG_H
#define DOG_H

#include "animal.h"

typedef struct Dog Dog;

/*
 * Derived class.
 *
 * The embedded Animal comes first, so a Dog pointer can be handed to any
 * function expecting an Animal, and Dog adds a breed of its own.
 *
 * Two things follow from that ordering. The ooc_object header inside Animal
 * stays at offset zero, where the library looks for it, and the upcast is a
 * no-op cast because both addresses are the same -- which is what lets speak()
 * in dog.c cast its Animal * back to a Dog * without arithmetic. The cost is
 * that a Dog may not add a member of its own before the base.
 *
 * The struct holds no destructor of its own; the one Dog registers lives in
 * Dog_class and releases both the breed and Animal's name.
 */
struct Dog {
    Animal animal;
    char  *breed;
};

/*
 * Dog's runtime type record, the value ooc_new() takes.
 *
 * Its `super` is Animal_class, so a Dog inherits Animal's fields, and its
 * `size` is sizeof(Dog), so the allocation has room for the breed.
 */
extern const ooc_class Dog_class;

/*
 * Create a Dog named `name` of the given `breed` and return it, or NULL.
 *
 * The result has a reference count of one, so ownership is the caller's and it
 * must reach ooc_release() eventually. Returns NULL if any step of the
 * construction fails, leaving nothing to release.
 *
 * The object is a Dog and answers to Dog's `speak`, whether it is later reached
 * through a Dog * or an Animal *.
 */
Dog *dog_new(const char *name, int age, const char *breed);

#endif /* DOG_H */
