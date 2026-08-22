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
BUILD    ?= obj

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
RUNTIME := lib/shmrt-$(TARGET).a
DEBUG_RUNTIME := lib/shmrt-$(TARGET)-debug.a

all: shc.exe $(RUNTIME) $(DEBUG_RUNTIME)

# shc.exe on every machine, not only Windows. The three programs in this family
# - RStudio, cc1 and shc - carry one name each wherever they are, and a suffix
# that changes by platform is one more thing every script has to know.
shc.exe: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(DEPENDS)

$(RUNTIME): $(RUNTIME_OBJECTS)
	@mkdir -p lib
	$(AR) rcs $@ $(RUNTIME_OBJECTS)

$(BUILD)/debug/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -DSHM_DEBUG=1 -c -o $@ $<

$(DEBUG_RUNTIME): $(DEBUG_RUNTIME_OBJECTS)
	@mkdir -p lib
	$(AR) rcs $@ $(DEBUG_RUNTIME_OBJECTS)

test: all
	./tests/run.sh

clean:
	rm -rf $(BUILD) shc.exe lib

.PHONY: all test clean
