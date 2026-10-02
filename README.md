# libooc

A tiny C library for object-oriented programming using ordinary C structs and 
function-pointer vtables. It provides object allocation, virtual destructors, 
intrusive reference counting, runtime class metadata, name-based field access, 
and a small interface header.

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

That is shorthand for `make DESTDIR="$PWD/pkg" PREFIX=/$(PREFIX) install`, and 
it honours an overridden prefix:

    make pkg PREFIX=/usr

## pkg-config

After installation:

    pkg-config --cflags --libs libooc

Compile an application with:

    cc -std=c99 -Wall -Wextra -Wpedantic main.c $(pkg-config --cflags --libs libooc)

If installed under `/usr/local` and your pkg-config does not search there:

    export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:$PKG_CONFIG_PATH

## Example

The `example/` directory holds a small class hierarchy. `Animal` is the root,
`Dog` and `Cat` derive from it, and `Garfield` and `Snoopy` derive from those:

    Animal
    |-- Dog
    |   `-- Snoopy
    `-- Cat
        `-- Garfield

Every class overrides the virtual `speak` method, and `example/main.c` walks each
one through the same calls before putting all four objects through a single
`Animal *`. The output makes the point that is invisible in the source: one call
site reaches four implementations, and the vtable decides which.

    make example-test  # runs example/main.c
    make safety-test   # runs the assertion suite in tests/

Objects are reference-counted with `ooc_retain()` and `ooc_release()`.

The public installed header is:

    #include <ooc/ooc.h>

Application-specific classes such as `Animal`, `Dog` and `Cat` are not installed 
by the library.

### Constructors down a chain

Each class publishes an `init` function alongside its constructor, so a subclass
builds the classes above it by calling them rather than repeating what they do:

```c
int garfield_init(Garfield *garfield, const char *name, int age,
                  const char *colour, const char *favourite_food);
```

`garfield_init()` calls `cat_init()`, which calls `animal_init()`. Each one
installs its own vtable, and the one above overwrites it, so the object ends up
speaking as the most derived class. A subclass of `Garfield` would call
`garfield_init()` in turn, and the composition extends without anything else
changing.

### Destructors down a chain

The library calls the destructor belonging to the object's runtime type and stops
there. A derived destructor is therefore responsible for the whole hierarchy, not
only for its own members, and the frees grow with the depth:

```c
static void destroy(ooc_object *object)
{
    Garfield *garfield = (Garfield *)object;

    free(garfield->favourite_food);   /* Garfield's */
    free(garfield->cat.colour);       /* Cat's, allocated by cat_init() */
    free(garfield->cat.animal.name);  /* Animal's, allocated by animal_init() */
}
```

Cat's own destructor never runs for a Garfield, and Animal's never runs for
either. Omitting a middle free is silent -- the object works perfectly well and
only a memory checker reports the leak, which is why `make safety-test` is worth
running under Valgrind or AddressSanitizer.

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

Lookup walks as far as the chain goes, and a class inserted in the middle hides
nothing. A `Garfield` resolves its own `favourite_food`, Cat's `colour` and
Animal's `name`, while `imagination` -- declared on the other branch -- resolves
to nothing at all:

```c
ooc_get(garfield, "colour");       /* Cat's */
ooc_get(garfield, "name");         /* Animal's, two records up */
ooc_get(garfield, "imagination");  /* NULL -- Snoopy's field, not in this chain */
```

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

ooc_set(dog, "name", &name);  /* "Rex" is freed here */
printf("%s\n", *(char **)ooc_get(dog, "name"));
```

Ownership passing means the replacement has to be `malloc()`ed memory and may
not be shared with a second field, or the same allocation would be freed twice.
Assigning a field the pointer it already holds frees nothing. The destructor
still frees whatever the field holds when the object is released.

The rule is the library's rather than the class', so it applies to an inherited
field exactly as to one declared alongside it. Setting a Garfield's inherited
`colour` releases the string Cat's constructor allocated, the same as setting its
own `favourite_food` does.

## Type checks

`ooc_is_a()` walks the inheritance chain from the object's runtime type, so it
answers for a base class as well as for the type itself:

```c
ooc_is_a(cat, &Cat_class);     /* 1 */
ooc_is_a(cat, &Animal_class);  /* 1 -- a Cat is an Animal */
ooc_is_a(cat, &Dog_class);     /* 0 -- siblings are unrelated */
```

The walk covers the whole chain, so depth costs nothing to the caller:

```c
ooc_is_a(garfield, &Garfield_class);  /* 1 */
ooc_is_a(garfield, &Cat_class);       /* 1 -- Garfield -> Cat -> Animal */
ooc_is_a(garfield, &Animal_class);    /* 1 */
ooc_is_a(garfield, &Snoopy_class);    /* 0 -- the other branch */
```

This is the useful direction to ask in, because an upcast in C is unchecked: a
caller holding an `Animal *` uses it to find out what the object really is.

Asking whether an object is *exactly* one type is a different question, and needs
no chain walk. The class record is a public member of `ooc_object`:

```c
cat->animal.object.class == &Animal_class;  /* 0 -- exactly a Cat */
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
    { NULL,     0,                        0,                             0 },
};
```

One underscore leaves a field readable but not writable, which is enough for a
class to show its own state without handing out a way to change it:

```c
ooc_set(dog, "_id", &id);     /* refused with -1, the field is unchanged */
*(int *)ooc_get(dog, "_id");  /* still readable */
```

Two underscores make the field unreachable in both directions. `ooc_get()`
still finds it in the table and then withholds the pointer, so there is no way
in by name, and only the object can change the value:

```c
ooc_get(dog, "__legs");         /* NULL, whether or not the field exists */
ooc_set(dog, "__legs", &legs);  /* refused with -1 */
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

The rules hold at any depth. `Garfield` has a `_meals` of its own while
inheriting Cat's `_lives`, and a caller can read both by name but write neither --
the rule belongs to the declaration, not to the object:

```c
ooc_get(garfield, "_meals");      /* readable -- Garfield's */
ooc_get(garfield, "_lives");      /* readable -- Cat's, two levels up */
ooc_set(garfield, "_meals", &n);  /* refused */
ooc_set(garfield, "_lives", &n);  /* also refused */
```

Private means private to the declaring class, which is why a subclass adds a
field of its own rather than leaning on the base class' private state.

Two things this is not. It does not keep a determined caller from reading a
single-underscore field, since `ooc_get()` hands back the field's real storage
and C has no access control -- the name is a contract, not a wall. And it does
not release anything: an owned private field is still owned, so a caller that
frees one behind the class' back leaves the destructor holding a dangling
pointer.

## Layout

    include/ooc/ooc.h   Public API
    src/ooc.c           Library implementation
    example/            Complete example: Animal, Dog, Cat, Garfield, Snoopy
    tests/safety.c      Assertion suite
    Makefile            Build/install rules
    libooc.pc.in        pkg-config template

Class metadata must remain alive and immutable while objects refer to it.
Cyclic inheritance and bases larger than their subclasses are rejected. Dynamic
fields must fit in their declaring class and must not overlap the object header.
Reference counting and field access require external synchronization across
threads. `ooc_retain()` returns NULL if the reference count cannot be incremented.

## Portability

The library API is standard C and has no third-party dependencies. The supplied 
Makefile uses conventional POSIX/Unix build tools and produces both shared and 
static libraries. Other platforms can use the same source/API with their native 
build system if their shared-library conventions differ.

## License

Apache License 2.0 — see the LICENSE file for details.

Copyright 2026 Johannes Findeisen
