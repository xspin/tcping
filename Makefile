# ===== Project configuration =====
APP        := tcping

VERSION    ?= 0.0.1

GETOPT_DIR ?= third_party/getopt

GIT_REV := $(shell git rev-parse --short HEAD)

# ===== Detect host OS =====
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
  PLATFORM := macos
else ifeq ($(UNAME_S),Linux)
  PLATFORM := linux
else ifneq (,$(findstring MINGW,$(UNAME_S)))
  PLATFORM := windows
else ifneq (,$(findstring MSYS,$(UNAME_S)))
  PLATFORM := windows
else
  PLATFORM := unknown
endif

# Install prefix, override via: make install PREFIX=/usr/local
PREFIX     ?= /usr/local


# ===== Common flags =====
CFLAGS     := -Wall -Wextra -std=c11
CPPFLAGS   := -DAPP_VERSION=\"$(VERSION)\" -DGIT_REV=\"$(GIT_REV)\"
LDFLAGS    :=
LDLIBS     :=


ifneq (,$(filter windows macos,$(PLATFORM)))
  SRCS     := $(GETOPT_DIR)/getopt.c $(GETOPT_DIR)/getopt1.c
  CPPFLAGS += -I$(GETOPT_DIR)
endif

SRCS       += $(wildcard src/*.c)
OBJS       := $(SRCS:.c=.o)
DEPS       := $(OBJS:.o=.d)

ifeq ($(PLATFORM),windows)
  LDLIBS     += -lws2_32
  LDFLAGS    += -static
else ifeq ($(PLATFORM),linux)
	# CPPFLAGS += -D_POSIX_C_SOURCE=200809L
	CPPFLAGS += -D_GNU_SOURCE=1
endif

# macOS architecture support
ARCH ?=

ifeq ($(PLATFORM),macos)
  ifneq ($(ARCH),)
    CFLAGS  += -arch $(ARCH)
    LDFLAGS += -arch $(ARCH)
  endif
endif

# ===== Target platform =====
# native: build for the host (default)
# mingw:  cross-compile for Windows using MinGW-w64
TARGET     ?= native

ifeq ($(TARGET),native)
  CC         := gcc
  EXEEXT     :=
  INSTALL    := install
else ifeq ($(TARGET),mingw)
  # Use the 64-bit toolchain. For 32-bit use i686-w64-mingw32-gcc.
  CC         := x86_64-w64-mingw32-gcc
  AR         := x86_64-w64-mingw32-ar
  EXEEXT     := .exe
  INSTALL    := cp
  # MinGW does not have /usr/local by default; use a local dist directory
  PREFIX     ?= dist/windows
  # Static linking avoids shipping libgcc/libwinpthread DLLs
  LDFLAGS    += -static
  CFLAGS 	 += -D_WIN32=1
  LDLIBS     += -lws2_32
else
  $(error TARGET must be native or mingw, got: $(TARGET))
endif

BINDIR     := $(PREFIX)/bin

# ===== Build mode =====
# Default is debug. Switch via: make BUILD=release
BUILD      ?= debug

ifeq ($(BUILD),debug)
  CFLAGS   += -g -O0 -DDEBUG
else ifeq ($(BUILD),release)
  CFLAGS   += -O2 -DNDEBUG
else
  $(error BUILD must be debug or release, got: $(BUILD))
endif

BIN        := $(APP)$(EXEEXT)

# ===== Targets =====
.PHONY: all clean install uninstall debug release help


all: $(BIN)

$(BIN): $(OBJS)
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

# Cross-compile for Windows, release build
mingw:
	$(MAKE) TARGET=mingw BUILD=release

install: $(BIN)
	$(INSTALL) -d $(DESTDIR)$(BINDIR)
	$(INSTALL) -m 755 $(BIN) $(DESTDIR)$(BINDIR)/$(BIN)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(OBJS) $(DEPS) $(BIN) $(APP).exe

help:
	@echo "Usage:"
	@echo "  make               # debug build (default)"
	@echo "  make BUILD=release # release build"
	@echo "  make mingw         # cross-compile, same as TARGET=mingw BUILD=release"
	@echo "  make debug         # same as BUILD=debug"
	@echo "  make release       # same as BUILD=release"
	@echo "  make install       # install to PREFIX/bin"
	@echo "  make clean         # remove build artifacts"
	@echo ""
	@echo "Overridable variables:"
	@echo "  PREFIX=/usr/local  # install prefix"
	@echo "  DESTDIR=/tmp/pkg   # staging directory for packaging"
