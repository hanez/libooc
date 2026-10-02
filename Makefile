PREFIX ?= /usr/local
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
PKGCONFIGDIR ?= $(LIBDIR)/pkgconfig
CC ?= cc
AR ?= ar
RANLIB ?= ranlib
VERSION ?= 0.1.0
SOVERSION ?= 0
CFLAGS ?= -std=c99 -O2
WARNINGS ?= -Wall -Wextra -Wpedantic
CPPFLAGS ?= -Iinclude
BUILD_DIR=build
PKG_DIR=pkg
OBJ=$(BUILD_DIR)/ooc.o
LIB_STATIC=$(BUILD_DIR)/libooc.a
LIB_SHARED=$(BUILD_DIR)/libooc.so.$(VERSION)
PKGCONFIG=$(BUILD_DIR)/libooc.pc

.PHONY: all shared static example example-test safety-test install uninstall pkg clean clean-pkg clean-all

all: shared static
$(BUILD_DIR):; mkdir -p $@
$(OBJ): src/ooc.c include/ooc/ooc.h | $(BUILD_DIR); $(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) -fPIC -c $< -o $@
$(LIB_STATIC): $(OBJ); $(AR) rcs $@ $^; $(RANLIB) $@
$(LIB_SHARED): $(OBJ); $(CC) $(CFLAGS) $(LDFLAGS) -shared -Wl,-soname,libooc.so.$(SOVERSION) -o $@ $^

shared: $(LIB_SHARED)
	ln -sf $$(basename $(LIB_SHARED)) $(BUILD_DIR)/libooc.so.$(SOVERSION)
	ln -sf $$(basename $(LIB_SHARED)) $(BUILD_DIR)/libooc.so

static: $(LIB_STATIC)
# FORCE, because PREFIX is a make variable rather than a file: without it a
# build/libooc.pc left over from another prefix is never rewritten and the
# staged .pc disagrees with the tree it sits in.
.PHONY: FORCE
FORCE:

$(PKGCONFIG): libooc.pc.in FORCE | $(BUILD_DIR); sed -e 's|@prefix@|$(PREFIX)|g' -e 's|@VERSION@|$(VERSION)|g' $< > $@

example: shared
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LDFLAGS) -Iinclude -Iexample example/main.c example/animal.c example/dog.c example/cat.c example/garfield.c example/snoopy.c -L$(BUILD_DIR) -Wl,-rpath,'$$ORIGIN' -looc -o $(BUILD_DIR)/example

EXAMPLE_SRCS=example/animal.c example/dog.c example/cat.c example/garfield.c example/snoopy.c
EXAMPLE_HDRS=example/animal.h example/dog.h example/cat.h example/garfield.h example/snoopy.h

$(BUILD_DIR)/safety-test: tests/safety.c $(EXAMPLE_SRCS) $(EXAMPLE_HDRS) $(LIB_STATIC)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WARNINGS) $(LDFLAGS) -Iexample tests/safety.c $(EXAMPLE_SRCS) $(LIB_STATIC) -o $@

example-test: example
	LD_LIBRARY_PATH=$(BUILD_DIR) $(BUILD_DIR)/example

safety-test: $(BUILD_DIR)/safety-test
	$(BUILD_DIR)/safety-test

install: all $(PKGCONFIG)
	install -d $(DESTDIR)$(LIBDIR) $(DESTDIR)$(INCLUDEDIR)/ooc $(DESTDIR)$(PKGCONFIGDIR)
	install -m 755 $(LIB_SHARED) $(DESTDIR)$(LIBDIR)/
	ln -sf $$(basename $(LIB_SHARED)) $(DESTDIR)$(LIBDIR)/libooc.so.$(SOVERSION)
	ln -sf libooc.so.$(SOVERSION) $(DESTDIR)$(LIBDIR)/libooc.so
	install -m 644 $(LIB_STATIC) $(DESTDIR)$(LIBDIR)/
	install -m 644 include/ooc/ooc.h $(DESTDIR)$(INCLUDEDIR)/ooc/
	install -m 644 $(PKGCONFIG) $(DESTDIR)$(PKGCONFIGDIR)/

# Stage the install tree under ./pkg for packaging, without touching the system.
# Equivalent to: make DESTDIR="$PWD/pkg" PREFIX=/$(PREFIX) install
# The prefix is re-rooted at / with exactly one separator, so an already
# absolute PREFIX does not end up as //usr/local inside the generated .pc file.
pkg: all
	$(MAKE) DESTDIR=$(CURDIR)/$(PKG_DIR) PREFIX=/$(patsubst /%,%,$(PREFIX)) install

uninstall:
	rm -f $(DESTDIR)$(LIBDIR)/libooc.so $(DESTDIR)$(LIBDIR)/libooc.so.$(SOVERSION) $(DESTDIR)$(LIBDIR)/$(notdir $(LIB_SHARED)) $(DESTDIR)$(LIBDIR)/$(notdir $(LIB_STATIC)) $(DESTDIR)$(INCLUDEDIR)/ooc/ooc.h $(DESTDIR)$(PKGCONFIGDIR)/libooc.pc

clean:
	rm -rf $(BUILD_DIR)

clean-pkg:
	rm -rf $(PKG_DIR)

clean-all: clean clean-pkg
