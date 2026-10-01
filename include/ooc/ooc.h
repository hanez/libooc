/*
 * This file is part of libooc.
 * https://xw3.org/hanez/libooc
 *
 * Copyright 2026 Johannes Findeisen <you@hanez.org>
 * Licensed under the terms of the Apache-2.0 license.
 * https://opensource.org/license/apache-2-0
 */

#ifndef OOC_H
#define OOC_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif
typedef struct ooc_object ooc_object;

typedef struct ooc_class ooc_class;

typedef struct ooc_field ooc_field;

/*
 * Handle for an interface object, holding the address of its vtable.
 *
 * The library does not read this struct: it is the convention for pointing at
 * a vtable of a known shape when the vtable's own type is not the caller's to
 * see. An object offering an interface embeds this as its first member, and a
 * caller casts the object to ooc_interface * to reach the vtable it names.
 * `vtable` is const because the table it points to is shared by every instance
 * of the class, not owned by any one of them.
 */
typedef struct ooc_interface { const void *vtable; } ooc_interface;

/*
 * Header stored at the very start of every object.
 *
 * `class` is the runtime type used for the virtual destructor and for dynamic
 * field lookup, `refs` is the intrusive reference count.
 *
 * Because the library reaches both through this header, it has to be the first
 * member of every object. A subclass embeds its base struct first rather than
 * the header directly, which is what makes an upcast between them a no-op cast:
 * both addresses are the same. Nothing else in the struct may precede it.
 */
struct ooc_object {
    const ooc_class *class;
    size_t refs;
};

/*
 * Description of a single field that ooc_get() and ooc_set() can reach.
 *
 * A class publishes its own fields as a NULL-terminated array of these.
 * `offset` and `size` locate the storage inside the object, so classes fill
 * them in with offsetof() and sizeof() instead of hand-computed numbers. Fields
 * must be nonempty, follow the object header, and fit in the declaring class.
 * Invalid descriptors are refused by both accessors. Owned fields must hold
 * exactly one object pointer compatible with free().
 *
 * `owned` marks a field as a single pointer the class owns. ooc_set() then
 * hands the field over to its new value instead of overwriting it blindly,
 * and releases the pointer that was there before. It stays zero for a value
 * that is merely copied around, such as an int or a pointer somebody else
 * keeps hold of. A zero `owned` is the default, so a table that lists nothing
 * but a name, an offset and a size is unchanged.
 *
 * A name starting with an underscore marks the field as belonging to the class
 * that declares it, which is how a class keeps a member to itself in a language
 * that has no access specifiers. One underscore hides the field from ooc_set(),
 * so it can be read but not written by name; two hide it from both ooc_set()
 * and ooc_get(), leaving the object the only thing that can reach the value.
 */
struct ooc_field {
    const char *name;
    size_t offset;
    size_t size;
    int    owned;
};

/*
 * Runtime type metadata.
 *
 * `size` is the size of the whole struct, subclasses included, and is what
 * ooc_new() allocates; `destroy` is the virtual destructor ooc_release() calls
 * once the last reference is gone, and is where a class releases whatever its
 * constructor allocated. A NULL `destroy` is allowed and frees the object
 * without further ado, which suits a class that owns nothing.
 *
 * `super` points at the base class, or NULL for a root class; field lookup
 * follows it, so a derived class inherits the fields of its ancestors. A NULL
 * `fields` array means the class publishes nothing dynamically.
 *
 * A class record is normally a file-scope constant. It is read but never
 * written by the library. The record, its ancestors, field tables and names
 * must remain alive and immutable until every referring object is released.
 * The inheritance chain must be acyclic and each base must fit in its subclass.
 */
struct ooc_class {
    size_t size;
    void (*destroy)(ooc_object *);
    const ooc_class *super;
    const ooc_field *fields;
};

/*
 * Create a zero-initialised instance of `class` and hand it to the caller.
 *
 * The object comes back with a reference count of one, so it is the caller's
 * to release, and every member behind the header is zero, so a subclass only
 * has to fill in what it cares about. No constructor runs: a subclass that
 * allocates members of its own does that itself, and releases the half-built
 * object if a later step fails.
 *
 * `class` must describe the class being instantiated and not one of its bases,
 * since its `size` is what gets allocated.
 *
 * Returns NULL if `class` is NULL, is too small to hold an object header,
 * has a cyclic inheritance chain or a base that does not fit, or allocation
 * fails. There is nothing to release in that case.
 */
void *ooc_new(const ooc_class *class);

/*
 * Take an additional reference on `object` and return it unchanged.
 *
 * The object survives until the last reference is released, which is what lets
 * a caller pass an object on without copying it. The returned pointer is
 * `object` itself, so a reference can be handed out in one expression.
 *
 * NULL is passed through and counted as nothing, so an optional object may be
 * retained unconditionally. Returns NULL without changing the count if it is
 * zero (destruction in progress) or SIZE_MAX (no additional reference fits).
 * Callers must check the result before treating it as a new reference.
 * Reference counting and field access require external synchronization when
 * an object is shared between threads.
 *
 * Only objects from ooc_new() are meant to be counted this way: a struct that
 * merely embeds an ooc_object header has no allocation behind it to be freed
 * with.
 */
void *ooc_retain(void *object);

/*
 * Drop one reference on `object` and free it once the last one is gone.
 *
 * The object's runtime destructor runs first, while the object's storage is
 * still intact, and is responsible for releasing everything the subclass
 * allocated; the library frees the object's own storage afterwards and nothing
 * else. A class that allocates members therefore needs a `destroy` of its own,
 * or those members leak.
 *
 * Destruction is not recursive and does not walk the class chain: the runtime
 * type's destructor is the only one that runs, so a subclass frees what it
 * inherited from its base itself.
 *
 * NULL and a zero count during destruction are ignored. Dead pointers cannot
 * be validated here, so releasing an object that other references still point at frees it under a live
 * pointer, and releasing one that is already gone is a use-after-free.
 */
void ooc_release(void *object);

/*
 * Drop one reference on `object` -- a synonym for ooc_release().
 *
 * Spelled the way an operator delete would read, for callers who think in
 * new/delete terms. It differs from ooc_release() in name only, so it does not
 * destroy the object unconditionally but waits for the last reference to go,
 * and NULL is ignored.
 */
void ooc_delete(void *object);

/*
 * Report whether `object` is an instance of `class` or of a subclass of it.
 *
 * The `super` chain is walked from the runtime type of the object, so a Cat
 * answers true for Cat_class, for Animal_class and for anything in between,
 * while a Dog answers false for Cat_class. This is how a caller holding an
 * Animal * finds out what an object really is, since an upcast in C is
 * unchecked, and how it asks "is this one of mine" about a base class.
 *
 * To ask whether an object is *exactly* one type rather than a subtype of it,
 * compare the object's public `class` member directly:
 *
 *     obj->class == &Animal_class
 *
 * A NULL object or a NULL `class` answers false.
 */
int ooc_is_a(const void *object, const ooc_class *class);

/*
 * Return a pointer to the storage of the named field, so callers can read or
 * write a field they only know by name.
 *
 * The object is all that is needed: its runtime class says where the field
 * sits, so the struct definition need not be visible to the caller.
 *
 * Lookup starts at the object's own class and continues up the inheritance
 * chain, so a Dog resolves "name" through Animal. A field a subclass declares
 * shadows a same-named field of its base, and because lookup starts at the
 * runtime type, the same name can reach two different fields depending on the
 * object -- the runtime type decides, not the static type of the pointer the
 * object arrived as.
 *
 * The returned pointer is the field's real storage and is writable, so it
 * stays valid for as long as the object does; the object therefore has to be a
 * non-const object. A pointer field is read in two steps: the slot comes back
 * first, the value it points to second.
 *
 * A field whose name starts with two underscores is out of reach: the lookup
 * finds it, but the pointer is withheld and NULL comes back instead, so the
 * value stays inside the class that declared it. One leading underscore does
 * not hide a field, it only stops ooc_set() writing one.
 *
 * Writing through the returned pointer bypasses ooc_set(), so a field that
 * declares itself owned is replaced through ooc_set() instead, or the value it
 * held is not released. It also bypasses the underscore rule for a field that
 * can be read in the first place, since the rule lives in ooc_set() rather than
 * in the pointer returned here.
 *
 * Returns NULL if the object is NULL, the name is NULL, no class in the chain
 * declares that field, the descriptor is invalid, or the field is hidden
 * behind two leading underscores.
 */
void *ooc_get(void *object, const char *field);

/*
 * Copy the value pointed to by `value` into the named field.
 *
 * `value` is the address of the value, exactly like the source argument of
 * memmove(), so overlapping source storage is allowed. A pointer field is
 * assigned through a pointer to the pointer:
 * ooc_set(dog, "name", &name). It must point to at least as many bytes as the
 * field occupies, normally a variable of the field's declared type, and
 * nothing beyond those bytes is read.
 *
 * The copy is all-or-nothing: a NULL argument, an unknown field name, or a
 * field of size zero is reported instead of written.
 *
 * A field that declares itself owned behaves like an assignment in C++: the
 * new value takes over and the old one goes away. ooc_set() copies the
 * replacement in first and then calls free() on the displaced pointer, so
 * source storage in the old allocation is readable until the copy finishes.
 * A field that is assigned the very same pointer again is left alone, making
 * it a no-op rather than a double free.
 *
 * Because ownership moves, the replacement must be NULL or memory from malloc()
 * and must point to the start of that allocation (never into the old allocation),
 * and once assigned no other field may be given the same pointer, or the same
 * allocation would be freed twice. A field that does not declare itself owned
 * is written blind and whatever it held stays the caller's to release. The
 * destructor still releases the value an owned field holds when the object is
 * destroyed.
 *
 * A field whose name starts with an underscore belongs to the class that
 * declares it and is not written here: the call is refused with -1. That is
 * the whole of a private member in a language without access specifiers. A name
 * that only contains an underscore further along is an ordinary field.
 *
 * Two leading underscores are the stronger form and are refused here as well;
 * those fields are also kept from ooc_get(), so nothing outside the class can
 * read or write them. Either way the refusal follows the name to whichever
 * class in the chain declares it, so a subclass cannot write a base class'
 * private field either. A class reaches its own fields through the struct
 * definition, where the members are in view and no accessor is needed.
 *
 * The descriptor of an owned field has to cover exactly one pointer. A wider
 * or narrower one is reported with -1 rather than partly released.
 *
 * Returns 0 on success and -1 when the write is refused, which covers every
 * case named above.
 */
int ooc_set(void *object, const char *field, const void *value);

#ifdef __cplusplus
}
#endif
#endif
