// Finding the rest of the program.
//
// Shalimar has no include, no import and no way to name another file, and it
// is not going to get one: telling the compiler where something is, is the
// compiler's work rather than the programmer's. So a call to a function this
// file does not define is not an error until the other files in the project
// have been looked in.
//
// What that buys is reuse without ceremony. A program becomes a library by
// renaming its main() to something a caller can say, and nothing else about
// it changes - no header to write, no list to keep up to date, no line at the
// top of the file that has to be right.
//
// Three rules make it predictable rather than magical:
//
//   1. **Only functions are looked for.** A global belongs to the file that
//      declares it. A function that is brought in brings its own file's
//      globals with it - the ones it actually reads - and can see no others.
//      Without that, two files could reach into each other's state and the
//      order they were found in would start to matter.
//
//   2. **Nothing arrives that was not asked for.** A function is brought in
//      because something called it, and then whatever it calls in turn. A
//      file's main() is therefore never brought in, since nothing can call a
//      function of that name - which is exactly why renaming it is what makes
//      the rest of that file reachable.
//
//   3. **Two files defining the same name is an error naming both.** Picking
//      one would make which file the compiler happened to read first into a
//      part of the language.
//
// What it costs is written down in docs/CROSSFILE.md: a program that reaches
// into another file no longer runs in the app, which has one file and no
// project. The compiler says which files it used so that this is never a
// surprise.
#pragma once

#include "Ast.h"
#include "Diag.h"

#include <memory>
#include <string>
#include <vector>

namespace shalimar {

// One parsed file.
struct Unit {
    std::string name;                 // as a diagnostic should print it
    std::unique_ptr<Program> program;
};

class Resolver {
public:
    Resolver(Diagnostics &diagnostics) : diag_(diagnostics) {}

    // Moves into `main` whatever it needs from `others`, and returns false
    // when something is wrong that only this pass can see - a name defined in
    // two files, or a name defined nowhere at all.
    //
    // A name found nowhere is left alone rather than reported here: the
    // checker says 'Unknown function' with a line to stand on, and this pass
    // has no line.
    bool resolve(Unit &main, std::vector<Unit> &others);

    // The files something was actually taken from, in the order they were
    // reached. What the compiler reports, so that a program which quietly
    // grew a dependency says so.
    const std::vector<std::string> &used() const { return used_; }

private:
    Diagnostics &diag_;
    std::vector<std::string> used_;
};

// Every function called anywhere in this program, by name.
std::vector<std::string> calledNames(Program &program);

// Every name a function's body mentions - which is more than it calls, and is
// what deciding whether it reads a global needs.
std::vector<std::string> mentionedNames(Function &function);

// What one statement calls, and what one function's body calls. Together they
// are the two halves of "which functions can a global's initializer reach" -
// the seeds and the edges - which is what decides where a global read has to
// be checked at run time. See CodeGen::reachableFromGlobals.
std::vector<std::string> calledNamesIn(Stmt &statement);
std::vector<std::string> calledNamesIn(Function &function);

}  // namespace shalimar
