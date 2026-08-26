# Compiler-S: the Shalimar compiler.
#
#   make            build shc.exe and the host runtime
#   make test       build, then run the suite
#   make clean
#
# ISO C++14, and pedantically so. A Mac cannot enforce that on its own -
# Apple's libc++ hands you C++17 names under -std=c++14 - so the same sources
# are built on the Linux box with real g++ before anything is believed.

CXX      ?= c++
CXXFLAGS ?= -std=c++14 -Wall -Wextra -Werror -pedantic -O2

# Header dependencies, and they are not optional. Without them an edit to a
# header rebuilds only the translation units make happens to think are older,
# and the ones it skips are left compiled against the previous definition of a
# class. The link succeeds - the mangled names still match - and the result
# corrupts the heap at run time, some way from anything that looks wrong. That
# happened here once and cost an hour chasing a sanitiser that had nothing to
# find, because a clean rebuild is exactly what a sanitiser build is.
DEPFLAGS := -MMD -MP
# Objects are built OUTSIDE the checkout, in a build directory beside the four
# projects: ../build/Compiler-S/obj. Nothing intermediate is ever written next
# to the sources, so `tar` on this repository carries source and nothing else,
# and a clean is a directory removal that cannot reach a tracked file.
#
# Overridable, and `?=` on purpose: workspace.mk names one place for all four,
# and a command line beats both.
BUILD    ?= ../build/Compiler-S/obj

HOST := $(shell uname -s)
ifeq ($(HOST),Darwin)
  TARGET ?= arm64-darwin
else
  TARGET ?= x86_64-linux
endif

SOURCES := \
  src/main.cpp \
  src/Driver.cpp \
  src/Diag.cpp \
  src/Lexer.cpp \
  src/Type.cpp \
  src/Ast.cpp \
  src/Parser.cpp \
  src/Check.cpp \
  src/Resolve.cpp \
  src/Builtin.cpp \
  runtime/Shortest.cpp \
  src/CodeGen.cpp \
  src/Target.cpp \
  src/backend/Emitter.cpp \
  src/backend/Spelling.cpp \
  src/backend/Arm64Darwin.cpp \
  src/backend/X86_64.cpp \
  src/backend/X86_64Linux.cpp \
  src/backend/X86_64Windows.cpp

# The runtime is built twice from the same sources. The release archive has
# no debugger code in it at all; the debug one is the same program plus a
# session that is dormant until the environment arms it. What the compiler
# emits does not differ between them by a byte - see docs/DEBUGGING.md.
RUNTIME_SOURCES := \
  runtime/Shortest.cpp \
  runtime/Failure.cpp \
  runtime/Numbers.cpp \
  runtime/Array.cpp \
  runtime/Console.cpp \
  runtime/Runtime.cpp

DEBUG_RUNTIME_SOURCES := $(RUNTIME_SOURCES) runtime/Debug.cpp

OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(SOURCES))
RUNTIME_OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(RUNTIME_SOURCES))
DEBUG_RUNTIME_OBJECTS := $(patsubst %.cpp,$(BUILD)/debug/%.o,$(DEBUG_RUNTIME_SOURCES))
DEPENDS := $(OBJECTS:.o=.d) $(RUNTIME_OBJECTS:.o=.d) $(DEBUG_RUNTIME_OBJECTS:.o=.d)
# Where the finished program goes. `.` is this directory, which is what every
# suite and script here already expects, so a plain `make` is unchanged. The
# workspace build names one directory instead, and all three programs are
# built into it rather than collected afterwards.
BINDIR ?= .
SHC    ?= $(BINDIR)/shc.exe

# **The runtime goes wherever the compiler goes, and that is not a preference.**
# shc finds its runtime relative to its own binary - lib/ beside it, then
# ../lib - so a shc.exe that arrives somewhere without its archives compiles,
# writes correct assembly, and then dies at the link naming a .a that is not
# there. Deriving both from BINDIR is what makes that impossible to get wrong;
# a copy rule written separately is what got it wrong before.
LIBDIR := $(BINDIR)/lib
RUNTIME := $(LIBDIR)/shmrt-$(TARGET).a
DEBUG_RUNTIME := $(LIBDIR)/shmrt-$(TARGET)-debug.a

all: $(SHC) $(RUNTIME) $(DEBUG_RUNTIME)

# shc.exe on every machine, not only Windows. The three programs in this family
# - RStudio, cc1 and shc - carry one name each wherever they are, and a suffix
# that changes by platform is one more thing every script has to know.
$(SHC): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(DEPENDS)

$(RUNTIME): $(RUNTIME_OBJECTS)
	@mkdir -p $(LIBDIR)
	$(AR) rcs $@ $(RUNTIME_OBJECTS)

$(BUILD)/debug/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -DSHM_DEBUG=1 -c -o $@ $<

$(DEBUG_RUNTIME): $(DEBUG_RUNTIME_OBJECTS)
	@mkdir -p $(LIBDIR)
	$(AR) rcs $@ $(DEBUG_RUNTIME_OBJECTS)

test: all
	SHC="$(abspath $(SHC))" ./tests/run.sh
# The examples too, because they were not built by anything and rotted: eight
# of the twelve stopped compiling when `uses` landed and nothing said so. They
# are documentation people are pointed at, so a broken one is worse than a
# broken case.
	SHC="$(abspath $(SHC))" ./tests/examples.sh
# The array ABI. Not a language check: it asks whether a C compiler can still
# read runtime/shmrt.h, whether the runtime still exports what
# docs/ARRAY-ABI.md names, and whether the two still link and agree. cc1 does
# the compiling where it is there, because it is stricter than the host's and
# it is the compiler a user of these tools already has.
	SHC="$(abspath $(SHC))" ./tests/abi.sh
# And the foreign declaration, which is not in tests/cases because that suite
# asserts shc and the app agree - and on this one construct they cannot.
	SHC="$(abspath $(SHC))" ./tests/foreign.sh

clean:
	rm -rf $(BUILD) $(SHC) $(LIBDIR)
	rm -rf tests/out tests/out-examples tests/out-abi tests/out-foreign

.PHONY: all test clean
