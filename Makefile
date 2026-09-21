# ===== Project configuration =====
APP        := tcping
SRCS       := $(wildcard *.c)
OBJS       := $(SRCS:.c=.o)
DEPS       := $(OBJS:.o=.d)

VERSION    ?= 0.0.1

# Install prefix, override via: make install PREFIX=/usr/local
PREFIX     ?= /usr/local
BINDIR     := $(PREFIX)/bin

# ===== Toolchain =====
CC         := gcc
INSTALL    := install

# ===== Common flags =====
CFLAGS     := -Wall -Wextra -std=c11
CPPFLAGS   := -DAPP_VERSION=\"$(VERSION)\"
LDFLAGS    :=
LDLIBS     :=

# ===== Build mode =====
# Default is release. Switch via: make BUILD=release
BUILD      ?= release

ifeq ($(BUILD),debug)
  CFLAGS   += -g -O0 -DDEBUG
else ifeq ($(BUILD),release)
  CFLAGS   += -O2 -DNDEBUG
else
  $(error BUILD must be debug or release, got: $(BUILD))
endif

# ===== Targets =====
.PHONY: all clean install uninstall debug release help

all: $(APP)

$(APP): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Auto-generate dependency files (.d) to track header changes
%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

-include $(DEPS)

# Convenience targets for build modes
debug:
	$(MAKE) BUILD=debug

release:
	$(MAKE) BUILD=release

install: $(APP)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(APP) $(DESTDIR)$(BINDIR)/$(APP)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(APP)

clean:
	rm -f $(OBJS) $(DEPS) $(APP)

help:
	@echo "Usage:"
	@echo "  make               # release build (default)"
	@echo "  make BUILD=release # release build"
	@echo "  make debug         # same as BUILD=debug"
	@echo "  make release       # same as BUILD=release"
	@echo "  make install       # install to PREFIX/bin"
	@echo "  make clean         # remove build artifacts"
	@echo ""
	@echo "Overridable variables:"
	@echo "  PREFIX=/usr/local  # install prefix"
	@echo "  DESTDIR=/tmp/pkg   # staging directory for packaging"
