# libooc

A tiny C library for object-oriented programming using ordinary C structs and function-pointer vtables. It provides object allocation, virtual destructors, intrusive reference counting, runtime class metadata, name-based field access, and a small interface header.

## Build

    make

Build the example:

    make test

## Install

The default prefix is `/usr/local`:

    sudo make install

Or install somewhere else:

    make PREFIX="$HOME/.local" install

For packaging, `pkg` stages the same tree under `./pkg` without needing root:

    make pkg
    make clean-pkg

That is shorthand for `make DESTDIR="$PWD/pkg" PREFIX=/$(PREFIX) install`, and it honours an overridden prefix:

    make pkg PREFIX=/usr

## pkg-config

After installation:

    pkg-config --cflags --libs libooc

Compile an application with:

    cc -std=c99 -Wall -Wextra -Wpedantic main.c $(pkg-config --cflags --libs libooc)

If installed under `/usr/local` and your pkg-config does not search there:

    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

## Example

The `example/` directory contains an `Animal` base class and two derived classes, `Dog` and `Cat`. Both override the virtual `speak` method, and `example/main.c` walks each of them through the same calls, so the output shows one call site reaching two different implementations. Objects are reference-counted with `ooc_retain()` and `ooc_release()`.

    make test            # runs example/main.c
    make safety-test     # runs the assertion suite in tests/

The public installed header is:

    #include <ooc/ooc.h>

Application-specific classes such as `Animal`, `Dog` and `Cat` are not installed by the library.

## Fields by name

A class publishes the fields that may be reached dynamically as a NULL-terminated
array of `ooc_field`, and names its base class in `ooc_class.super`:

```c
static const ooc_field Animal_fields[] = {
    { "name", offsetof(Animal, name), sizeof(((Animal *)0)->name), 1 },
    { "age",  offsetof(Animal, age),  sizeof(((Animal *)0)->age),  0 },
    { NULL,   0,                      0,                           0 },
};

const ooc_class Animal_class = {
    .size    = sizeof(Animal),
    .destroy = destroy,
    .super   = NULL,
    .fields  = Animal_fields,
};
```

That is enough to read and write fields through the object alone, without the
struct definition being visible:

```c
int age = 6;

ooc_set(dog, "age", &age);
printf("%s is %d\n", *(char **)ooc_get(dog, "name"), *(int *)ooc_get(dog, "age"));
```

`ooc_get()` returns the address of the field, so lookup follows the inheritance
chain: a `Dog` resolves `name` and `age` through `Animal_class`, and a field
declared by a subclass shadows a same-named field of its base. Upcasting a
pointer does not change this -- the runtime type of the object decides.

`ooc_set()` copies the field's storage with overlap-safe semantics and returns `-1` for an
unknown field name or a NULL argument, so a bad name is reported instead of
writing out of bounds.

The last column of a field descriptor says who owns the value. Zero, the
default, is a value that is only copied around and has to be freed by whoever
allocated it. Non-zero marks a field as a single pointer the class owns, and
`ooc_set()` then hands the field over: the new value is copied in and the old
pointer is freed. Since `ooc_set()` takes the address of the value, a pointer
field is assigned through a pointer to the pointer:

```c
char *name = copy_of("Bella");

ooc_set(dog, "name", &name);          /* "Rex" is freed here */
printf("%s\n", *(char **)ooc_get(dog, "name"));
```

Ownership passing means the replacement has to be `malloc()`ed memory and may
not be shared with a second field, or the same allocation would be freed twice.
Assigning a field the pointer it already holds frees nothing. The destructor
still frees whatever the field holds when the object is released.

## Type checks

`ooc_is_a()` walks the inheritance chain from the object's runtime type, so it
answers for a base class as well as for the type itself:

```c
ooc_is_a(cat, &Cat_class);      /* 1 */
ooc_is_a(cat, &Animal_class);   /* 1 -- a Cat is an Animal */
ooc_is_a(cat, &Dog_class);      /* 0 -- siblings are unrelated */
```

This is the useful direction to ask in, because an upcast in C is unchecked: a
caller holding an `Animal *` uses it to find out what the object really is.

Asking whether an object is *exactly* one type is a different question, and needs
no chain walk. The class record is a public member of `ooc_object`:

```c
cat->animal.object.class == &Animal_class;   /* 0 -- exactly a Cat */
```

## Private fields

A field whose name starts with an underscore belongs to the class that declares
it. The number of underscores is how much of the field the outside world gets,
and it is the convention that stands in for access specifiers in a language
that has none:

| Name | `ooc_get()` | `ooc_set()` |
| --- | --- | --- |
| `name` | reads | writes, and releases the old value if owned |
| `_id` | reads | refused |
| `__legs` | refused | refused |
| `has_underscore` | reads | writes |

Both fields are published in the field table, so the library knows where they
are and can enforce the rest:

```c
static const ooc_field Animal_fields[] = {
    { "name",   offsetof(Animal, name),   sizeof(((Animal *)0)->name),   1 },
    { "_id",    offsetof(Animal, _id),    sizeof(((Animal *)0)->_id),    0 },
    { "__legs", offsetof(Animal, __legs), sizeof(((Animal *)0)->__legs), 0 },
    { NULL,     0,                        0,                            0 },
};
```

One underscore leaves a field readable but not writable, which is enough for a
class to show its own state without handing out a way to change it:

```c
ooc_set(dog, "_id", &id);         /* refused with -1, the field is unchanged */
*(int *)ooc_get(dog, "_id");      /* still readable */
```

Two underscores make the field unreachable in both directions. `ooc_get()`
still finds it in the table and then withholds the pointer, so there is no way
in by name, and only the object can change the value:

```c
ooc_get(dog, "__legs");            /* NULL, whether or not the field exists */
ooc_set(dog, "__legs", &legs);     /* refused with -1 */
```

What a class keeps under such a name it hands out itself, through an accessor
the way a method would:

```c
int animal_legs(const Animal *animal)
{
    return animal ? animal->__legs : 0;
}
```

Both rules follow the name to whichever class in the chain declared it, so a
subclass cannot reach a base class' private field either. A class writes its
own fields through the struct definition, where the members are in view and no
accessor is needed.

Two things this is not. It does not keep a determined caller from reading a
single-underscore field, since `ooc_get()` hands back the field's real storage
and C has no access control -- the name is a contract, not a wall. And it does
not release anything: an owned private field is still owned, so a caller that
frees one behind the class' back leaves the destructor holding a dangling
pointer.

## Layout

    include/ooc/ooc.h   Public API
    src/ooc.c           Library implementation
    example/            Complete example
    Makefile            Build/install rules
    libooc.pc.in        pkg-config template

Class metadata must remain alive and immutable while objects refer to it.
Cyclic inheritance and bases larger than their subclasses are rejected. Dynamic
fields must fit in their declaring class and must not overlap the object header.
Reference counting and field access require external synchronization across
threads. `ooc_retain()` returns NULL if the reference count cannot be incremented.

## Portability

The library API is standard C and has no third-party dependencies. The supplied Makefile uses conventional POSIX/Unix build tools and produces both shared and static libraries. Other platforms can use the same source/API with their native build system if their shared-library conventions differ.

## License

Apache License 2.0 — see the LICENSE file for details.

Copyright 2026 Johannes Findeisen
