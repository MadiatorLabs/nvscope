CC ?= cc
USER_CPPFLAGS := $(CPPFLAGS)
USER_CFLAGS := $(CFLAGS)
REQUIRED_CPPFLAGS := -D_GNU_SOURCE
REQUIRED_CFLAGS := -std=c11 -Wall -Wextra -Werror -O2 -fPIC
LDFLAGS_SO ?= -shared -ldl -pthread

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
LIBDIR ?= $(PREFIX)/lib/nvscope
DOCDIR ?= $(PREFIX)/share/doc/nvscope

LIB := libnvscope.so
SRC := $(wildcard src/*.c)
OBJ := $(SRC:.c=.o)

.PHONY: all clean install uninstall test

all: $(LIB)

$(LIB): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDFLAGS_SO)

src/%.o: src/%.c
	$(CC) $(REQUIRED_CPPFLAGS) $(USER_CPPFLAGS) $(REQUIRED_CFLAGS) $(USER_CFLAGS) -c $< -o $@

test:
	@set -eu; for t in tests/test_*.sh; do echo "==> $$t"; sh "$$t"; done

install: $(LIB)
	install -d "$(DESTDIR)$(BINDIR)" "$(DESTDIR)$(LIBDIR)" "$(DESTDIR)$(DOCDIR)"
	install -m 0755 "$(LIB)" "$(DESTDIR)$(LIBDIR)/$(LIB)"
	install -m 0755 tools/nvscope "$(DESTDIR)$(BINDIR)/nvscope"
	install -m 0755 tools/nvscope-probe "$(DESTDIR)$(BINDIR)/nvscope-probe"
	install -m 0644 README.md LICENSE "$(DESTDIR)$(DOCDIR)/"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/nvscope" "$(DESTDIR)$(BINDIR)/nvscope-probe"
	rm -f "$(DESTDIR)$(LIBDIR)/$(LIB)"

clean:
	rm -f $(LIB) src/*.o
	rm -rf build tmp
