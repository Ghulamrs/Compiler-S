# Compiler-S: the Shalimar compiler.
#
#   make            build shc and the host runtime
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
BUILD    ?= build

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
  src/Builtin.cpp \
  src/CodeGen.cpp \
  src/Target.cpp \
  src/backend/Emitter.cpp \
  src/backend/Spelling.cpp \
  src/backend/Arm64Darwin.cpp \
  src/backend/X86_64.cpp \
  src/backend/X86_64Linux.cpp \
  src/backend/X86_64Windows.cpp

RUNTIME_SOURCES := \
  runtime/Failure.cpp \
  runtime/Numbers.cpp \
  runtime/Array.cpp \
  runtime/Console.cpp \
  runtime/Runtime.cpp

OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(SOURCES))
RUNTIME_OBJECTS := $(patsubst %.cpp,$(BUILD)/%.o,$(RUNTIME_SOURCES))
DEPENDS := $(OBJECTS:.o=.d) $(RUNTIME_OBJECTS:.o=.d)
RUNTIME := lib/shmrt-$(TARGET).a

all: shc $(RUNTIME)

shc: $(OBJECTS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJECTS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c -o $@ $<

-include $(DEPENDS)

$(RUNTIME): $(RUNTIME_OBJECTS)
	@mkdir -p lib
	$(AR) rcs $@ $(RUNTIME_OBJECTS)

test: all
	./tests/run.sh

clean:
	rm -rf $(BUILD) shc lib

.PHONY: all test clean
