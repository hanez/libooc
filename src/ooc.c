/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

/*
 * libooc -- a very small object-orientation layer for plain C.
 *
 * Provides allocation, virtual destruction, intrusive reference counting and
 * name-based field access for objects that embed an `ooc_object` header and
 * describe themselves with a runtime `ooc_class` record.
 */

#include "ooc/ooc.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Report whether a class record and its ancestors describe a usable hierarchy.
 *
 * Everything the runtime does with a class record -- allocating from its size,
 * calling its destructor, walking its inheritance chain, resolving a field
 * against it -- reads the record and the records it points at, so a malformed
 * one is a memory-safety problem rather than a wrong answer. The checks here
 * are what a caller of the API cannot be trusted to have got right, since a
 * class record is hand-written data like any other. Metadata is expected to
 * stay alive and immutable for as long as its objects exist, so this can only
 * look at the shape of it.
 *
 * Three things are rejected. A NULL class has nothing to describe. A cycle in
 * the `super` chain would make every walk of it loop forever, so the chain is
 * checked with the tortoise-and-hare method before it is followed: one pointer
 * steps once per iteration and the other twice, and a chain that loops brings
 * them together. Finally every class on the chain has to be able to hold an
 * object header, and a derived class may not be smaller than its base -- a base
 * that does not fit inside the derived object would have its fields written
 * past the end of the allocation.
 *
 * Returns 1 for a sound hierarchy and 0 otherwise. A chain that is merely
 * broken in some other way, a class whose `size` does not match its struct for
 * instance, cannot be detected from the record alone and is not checked.
 */
static int valid_class(const ooc_class *class)
{
    const ooc_class *slow = class;
    const ooc_class *fast = class;

    if (!class)
        return 0;

    /* Reject cycles before walking the chain. */
    while (fast && fast->super) {
        slow = slow->super;
        fast = fast->super->super;
        if (slow == fast)
            return 0;
    }

    for (; class; class = class->super) {
        if (class->size < sizeof(ooc_object) ||
            (class->super && class->super->size > class->size))
            return 0;
    }
    return 1;
}

/*
 * Report whether a field descriptor stays inside the object of `class`.
 *
 * A descriptor is the one piece of a class record that is turned into a real
 * address -- `object + offset` -- and the width it claims is turned into a
 * copy, so a descriptor whose numbers do not describe the struct gives out a
 * pointer into unrelated memory and reads or writes through it. The descriptor
 * is checked against the class it was found in, which is the object that will
 * be addressed: a field inherited from a base is bounded by the derived class,
 * which is the larger of the two.
 *
 * The rules are that the field occupies some bytes, sits at or after the end of
 * the object header -- an offset of 0 would let a name-based write overwrite the
 * runtime type and the reference count, and with them the object's own
 * bookkeeping -- and ends within the object. That last test is written as a
 * subtraction rather than as `offset + size <= class->size`, because adding
 * first wraps around for a pair of large values and a wrapped sum would pass
 * the check it was meant to fail. A field marked owned has to be exactly one
 * pointer wide, since that is the width ooc_set() frees, and freeing part of a
 * wider field or more than one of its pointers would corrupt the heap.
 *
 * Returns 1 for a descriptor that is safe to address and 0 otherwise. Note that
 * a rejected field makes its *name* unusable: lookup stops at the first match
 * and reports it as unknown, rather than continuing to an ancestor that happens
 * to declare the same name.
 */
static int valid_field(const ooc_class *class, const ooc_field *field)
{
    /* Subtraction avoids overflow in offset + size. Protect the header too. */
    return field->size != 0 && field->offset >= sizeof(ooc_object) &&
           field->offset <= class->size &&
           field->size <= class->size - field->offset &&
           (!field->owned || field->size == sizeof(void *));
}

/*
 * Create a zero-initialised instance of `class`.
 *
 * The object gets a header -- its runtime type and a reference count of one --
 * and calloc() zeroes everything behind it, so a subclass only has to fill in
 * the members it actually cares about. No constructor runs, so a subclass that
 * allocates members of its own does that itself, and releases the half-built
 * object again if a later step fails.
 *
 * `class` has to describe the subclass and not one of its bases: `size` is
 * what gets allocated, so a class reporting the size of its base hands out an
 * object too small for the members declared below it.
 *
 * The new object starts out with a reference count of one, so ownership is
 * handed to the caller, which must eventually pass it to `ooc_release`.
 *
 * Returns NULL if `class` is missing, is too small to hold an object header,
 * or if the allocation fails. There is nothing to release in that case, since
 * no object came into being.
 */
void *ooc_new(const ooc_class *class)
{
    ooc_object *object;

    if (!valid_class(class))
        return NULL;

    object = calloc(1, class->size);
    if (!object)
        return NULL;

    object->class = class;
    object->refs  = 1;

    return object;
}

/*
 * Take an additional reference on `object`.
 *
 * The count sits in the header at the very start of every object, so a
 * reference can be taken through a pointer to any of its base classes: the
 * upcast costs nothing and the count lands on the same header either way. The
 * count is a number of references rather than of owners, which is what lets an
 * object outlive the code that created it and die with the last holder.
 *
 * The object is returned unchanged, so a reference can be handed on in a
 * single expression. Count zero or SIZE_MAX returns NULL without incrementing.
 * NULL is passed through, so optional objects may be
 * retained unconditionally.
 *
 * Only objects that came out of `ooc_new` are meant to be counted this way; a
 * struct that merely embeds the header has no allocation behind it to survive
 * or be freed with.
 */
void *ooc_retain(void *object)
{
    ooc_object *self = object;

    if (!self || self->refs == 0 || self->refs == SIZE_MAX)
        return NULL;

    ++self->refs;
    return object;
}

/*
 * Drop one reference on `object`.
 *
 * Once the last reference is gone the class' virtual destructor runs -- it is
 * responsible for releasing everything the subclass allocated -- and the
 * object itself is freed. NULL is ignored.
 *
 * Destruction is the counterpart of the reference `ooc_new` hands out, so every
 * reference taken by `ooc_new` or `ooc_retain` wants one release back. The
 * balance is the caller's to keep and cannot be checked from here: releasing an
 * object whose other references are still in use frees it under a live pointer,
 * and releasing one that is already gone is a use-after-free.
 *
 * Releasing is not recursive and does not climb the class chain by itself. The
 * destructor of the runtime type is the one that runs, and a subclass that
 * inherits members from a base has to free those itself, as the example's Dog
 * does for Animal's name.
 */
void ooc_release(void *object)
{
    ooc_object *self = object;

    if (!self || self->refs == 0)
        return;

    if (--self->refs == 0) {
        if (self->class && self->class->destroy)
            self->class->destroy(self);

        free(self);
    }
}

/*
 * Drop one reference on `object`.
 *
 * Synonym for `ooc_release`, spelled the way an operator delete would read.
 *
 * The name is the only difference, and it is the caller's shorthand: this
 * destroys nothing on its own but drops a reference, so the object goes away
 * when the last one does, exactly as `ooc_release` behaves. NULL is ignored.
 */
void ooc_delete(void *object)
{
    ooc_release(object);
}

/*
 * Report whether `object` is an instance of `class` or of any subclass of it.
 *
 * The search walks the `super` chain from the runtime type of the object, so a
 * Dog answers true for Animal_class as well as for Dog_class. That is the
 * useful direction to answer in: an upcast in C is unchecked, so this is how a
 * caller holding an Animal * finds out what the object really is, and asking
 * about a base class is how it asks "is this one of mine".
 *
 * The converse test -- is this *exactly* this type -- is deliberately not what
 * this function does. A class record is public in ooc_object, so a caller who
 * needs the exact type compares obj->class against the record itself and gets
 * the answer without a chain walk.
 *
 * A NULL object or a NULL class answers false, and a class whose chain is
 * unsound is refused by the same validation ooc_new() applies, so a cyclic
 * hierarchy cannot make this loop. The result is a plain yes-or-no.
 */
int ooc_is_a(const void *object, const ooc_class *class)
{
    const ooc_object *self = object;
    const ooc_class  *step;

    if (!self || !class)
        return 0;

    if (!valid_class(self->class) || !valid_class(class))
        return 0;

    for (step = self->class; step; step = step->super)
        if (step == class)
            return 1;

    return 0;
}

/*
 * Find a field by name, starting at `class` and continuing up the inheritance
 * chain.
 *
 * Fields declared by a class shadow same-named fields of its ancestors, and
 * because the search starts at the runtime type of the object, the same name
 * can reach two different fields depending on which class is asked. The
 * descriptor that comes back belongs to the class and is const, so it stays
 * valid for as long as that class does.
 *
 * This is a strcmp() per field along the chain, which is the point of a
 * reflection layer and wasted effort wherever the struct definition is known
 * at compile time and the member can simply be named.
 *
 * Returns NULL if either argument is NULL or no class in the chain knows the
 * name.
 */
static const ooc_field *find_field(const ooc_class *class, const char *name)
{
    if (!name || !valid_class(class))
        return NULL;

    for (; class; class = class->super) {
        const ooc_field *field;

        for (field = class->fields; field && field->name; field++)
            if (strcmp(field->name, name) == 0)
                return valid_field(class, field) ? field : NULL;
    }

    return NULL;
}

/*
 * Report whether a field name is reserved for the class that declares it.
 *
 * A leading underscore marks a field as internal to the class, so callers
 * outside it can look but not touch: the underscore is the convention that
 * stands in for a private member in a language without access specifiers. The
 * rule is a leading underscore, so both `_count` and `__count` are reserved,
 * while a name that merely contains one further along is not.
 */
static int is_private_field(const char *name)
{
    return name && name[0] == '_';
}

/*
 * Report whether a field name is out of reach even to read.
 *
 * Two leading underscores are the stronger form: a name reserved to the class
 * on both sides, which ooc_get() will not hand out and ooc_set() will not
 * write. Reading is refused along with writing, so what a class keeps under
 * such a name stays inside it and the object is the only thing that can reach
 * the value.
 */
static int is_hidden_field(const char *name)
{
    return name && name[0] == '_' && name[1] == '_';
}

/*
 * Return the address of the named field's storage.
 *
 * The object is all that is needed: its runtime class says where the field
 * sits, so a caller that has never seen the struct definition can still reach
 * the field. Lookup starts at the object's own class and continues up the
 * inheritance chain, so a field a subclass declares shadows a same-named field
 * of its base -- the runtime type of the object decides, not the static type
 * of the pointer it arrived as.
 *
 * The result is the field's real storage and not a copy of it, so it stays
 * valid for as long as the object does and may be written through. A pointer
 * field is therefore read in two steps: the slot comes back first, the value
 * it points to second. Writing through the returned pointer sidesteps
 * ooc_set(), so an owned field is replaced through ooc_set() instead, or the
 * value it held is not released.
 *
 * A field whose name starts with two underscores is out of reach here: the
 * lookup still finds it, but the pointer is withheld and NULL comes back
 * instead, so the value stays inside the class that declared it. A single
 * leading underscore does not hide a field, it only stops ooc_set() writing
 * one.
 *
 * Returns NULL if the object is NULL, the name is NULL, no class in the chain
 * declares the field, or the field is hidden behind two leading underscores.
 */
void *ooc_get(void *object, const char *field)
{
    const ooc_field *descriptor;

    if (!object)
        return NULL;

    descriptor = find_field(((ooc_object *)object)->class, field);
    if (!descriptor || is_hidden_field(descriptor->name))
        return NULL;

    return (char *)object + descriptor->offset;
}

/*
 * Copy the value pointed to by `value` into the named field.
 *
 * `value` is the address of the value, as with memcpy(), so a pointer field is
 * assigned through a pointer to the pointer and a name is replaced by
 * ooc_set(dog, "name", &name). Nothing is read out of `value` beyond the bytes
 * the field occupies, which is why the caller's variable has to be of the
 * field's declared type.
 *
 * A field that declares itself owned is handed over rather than overwritten:
 * the copy lands first, so source storage in the old allocation stays readable
 * until copying finishes, and the displaced pointer is freed afterwards
 * unless the field is being assigned the very same pointer again. A field that
 * does not declare itself owned is written blind, and whatever its old value
 * was stays the caller's to release.
 *
 * A field whose name starts with an underscore belongs to the class that
 * declares it, and writing it by name is refused, whether the name is reached
 * through this call or was found in a base class. Reading it stays possible,
 * so ooc_get() still reaches a private field.
 *
 * Returns 0 on success and -1 when the write is refused: a NULL object or
 * value, a name no class in the chain knows, a name reserved to the class
 * declaring it, a field of size zero, or an owned field whose descriptor does
 * not cover exactly one pointer.
 */
int ooc_set(void *object, const char *field, const void *value)
{
    const ooc_field *descriptor;

    if (!object || !value)
        return -1;

    descriptor = find_field(((ooc_object *)object)->class, field);
    if (!descriptor || descriptor->size == 0)
        return -1;

    if (is_private_field(descriptor->name))
        return -1;

    if (descriptor->owned) {
        void *old;
        void *replacement;

        /*
         * An owned field is one pointer, so a descriptor covering more or
         * less is refused here instead of having part of a wider field freed.
         */
        if (descriptor->size != sizeof(old))
            return -1;

        /*
         * The declared type of the field is not known here, so both pointers
         * travel through memcpy() rather than through a cast.
         */
        memcpy(&old, (char *)object + descriptor->offset, sizeof(old));
        memcpy(&replacement, value, sizeof(replacement));

        memcpy((char *)object + descriptor->offset, &replacement, sizeof(replacement));

        /* Assigning the same pointer back replaces nothing. */
        if (old != replacement)
            free(old);

        return 0;
    }

    memmove((char *)object + descriptor->offset, value, descriptor->size);

    return 0;
}
