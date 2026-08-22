#include "Driver.h"

#include "Check.h"
#include "CodeGen.h"
#include "Diag.h"
#include "Parser.h"
#include "Resolve.h"
#include "Target.h"
#include "Token.h"
#include "backend/Emitter.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

namespace shalimar {

void Driver::usage() const {
    std::cerr <<
        "usage: shc [options] file.shm\n"
        "\n"
        "  -o <path>          where the result goes\n"
        "  -S                 stop after writing assembly\n"
        "  -c                 stop after assembling\n"
        "  --target=<name>    arm64-darwin | x86_64-linux | x86_64-windows\n"
        "  --runtime=<path>   the runtime archive to link against\n"
        "  --no-search        do not look in the other files beside this one\n"
        "  --debug            link the runtime a debugger can stop, which\n"
        "                     changes nothing about what is compiled\n"
        "\n"
        "  A call to a function this file does not define is looked for in the\n"
        "  other Shalimar files beside it, and what is found is compiled in.\n"
        "  There is nothing to declare and nothing to list: a program becomes\n"
        "  a library by renaming its main() to something a caller can say.\n"
        "  Naming files after the program uses those instead of looking.\n";
}

std::string Driver::stem(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
}

std::string Driver::leafOf(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Both separators, deliberately. Being hosted on a platform is a separate
// axis from targeting it, and a driver that knows only '/' is a bug that
// cannot be reached from the machine it is usually written on.
std::string Driver::directoryOf(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool Driver::readFile(const std::string &path, std::string &into) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    into = buffer.str();
    return true;
}

bool Driver::writeFile(const std::string &path, const std::string &text) {
    std::ofstream out(path.c_str(), std::ios::binary);
    if (!out) return false;
    out << text;
    return out.good();
}

bool Driver::looksLikeShalimar(const std::string &name) {
    const char *suffixes[] = {".shl", ".shm"};
    for (int i = 0; i < 2; ++i) {
        const size_t n = std::string(suffixes[i]).size();
        if (name.size() > n && name.compare(name.size() - n, n, suffixes[i]) == 0) return true;
    }
    return false;
}

// What 'the project' means to a compiler that was handed one file: the other
// Shalimar files beside it. Sorted, so that two runs agree about everything
// including which of two clashing files a diagnostic is reported against.
std::vector<std::string> Driver::shalimarFilesIn(const std::string &directory) {
    std::vector<std::string> found;
#ifdef _WIN32
    WIN32_FIND_DATAA entry;
    HANDLE handle = FindFirstFileA((directory + "\\*").c_str(), &entry);
    if (handle == INVALID_HANDLE_VALUE) return found;
    do {
        if (looksLikeShalimar(entry.cFileName))
            found.push_back(directory + "\\" + entry.cFileName);
    } while (FindNextFileA(handle, &entry));
    FindClose(handle);
#else
    DIR *open = opendir(directory.c_str());
    if (!open) return found;
    while (struct dirent *entry = readdir(open)) {
        if (looksLikeShalimar(entry->d_name))
            found.push_back(directory + "/" + entry->d_name);
    }
    closedir(open);
#endif
    std::sort(found.begin(), found.end());
    return found;
}

int Driver::shell(const std::string &command) {
    return std::system(command.c_str());
}

std::string Driver::defaultRuntimeObject(const std::string &targetName) const {
    return directoryOf(program_) + "/lib/shmrt-" + targetName +
           (debug_ ? "-debug" : "") + ".a";
}

bool Driver::parseArguments(const std::vector<std::string> &arguments) {
    for (size_t i = 1; i < arguments.size(); ++i) {
        const std::string &a = arguments[i];
        if (a == "-o" && i + 1 < arguments.size()) {
            output_ = arguments[++i];
        } else if (a == "-S") {
            assemblyOnly_ = true;
        } else if (a == "-c") {
            objectOnly_ = true;
        } else if (a.compare(0, 9, "--target=") == 0) {
            targetName_ = a.substr(9);
        } else if (a.compare(0, 10, "--runtime=") == 0) {
            runtimeObject_ = a.substr(10);
        } else if (a == "--no-search") {
            search_ = false;
        } else if (a == "--debug") {
            debug_ = true;
        } else if (a == "-h" || a == "--help") {
            usage();
            return false;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "shc: unknown option " << a << "\n";
            return false;
        } else if (input_.empty()) {
            input_ = a;
        } else {
            // The first file is the program; the rest are where to look. Only
            // one main() is ever compiled, and it is the first file's.
            companions_.push_back(a);
        }
    }
    if (input_.empty()) {
        usage();
        return false;
    }
    if (targetName_.empty()) targetName_ = Target::hostName();
    return true;
}

int Driver::run(const std::vector<std::string> &arguments) {
    program_ = arguments.empty() ? "shc" : arguments[0];
    if (!parseArguments(arguments)) return 2;

    std::unique_ptr<Target> target = Target::forName(targetName_);
    if (!target) {
        std::cerr << "shc: unknown target " << targetName_ << "\n";
        return 2;
    }

    // The program, and the other files it may reach into. Named ones win; a
    // program named alone gets the ones beside it, unless --no-search.
    std::vector<std::string> files;
    files.push_back(input_);
    if (!companions_.empty()) {
        for (const std::string &name : companions_) files.push_back(name);
    } else if (search_) {
        for (const std::string &beside : shalimarFilesIn(directoryOf(input_))) {
            if (beside != input_ && stem(beside) != stem(input_)) files.push_back(beside);
        }
    }

    Diagnostics diagnostics;
    std::vector<std::string> names;
    for (const std::string &path : files) names.push_back(leafOf(path));
    diagnostics.nameUnits(names);

    std::vector<Unit> units;
    for (size_t i = 0; i < files.size(); ++i) {
        std::string source;
        if (!readFile(files[i], source)) {
            std::cerr << "shc: cannot read " << files[i] << "\n";
            return 2;
        }

        // Lex. Tokenizing stops at the offending character, so nothing
        // downstream is trustworthy and the error is reported alone.
        LexResult lexed = tokenize(source);
        if (lexed.failed) {
            // A file the compiler went looking in is not the program, and a
            // program should not be refused because something else in the
            // directory does not lex. It is dropped, and said so.
            if (i > 0) {
                std::cerr << "shc: ignoring " << names[i] << ": " << lexed.error << "\n";
                continue;
            }
            diagnostics.error(static_cast<int>(i), lexed.errorLine, lexed.error);
            std::string text;
            diagnostics.writeTo(text);
            std::cout << text;
            return 1;
        }

        // Parse. Reported one at a time; parsing stops at the first.
        Diagnostics quiet;
        Parser parser(lexed.tokens, i == 0 ? diagnostics : quiet, static_cast<int>(i));
        std::unique_ptr<Program> parsed = parser.parse();
        if (i > 0) {
            if (!parsed) {
                std::string why;
                quiet.writeTo(why);
                if (!why.empty() && why[why.size() - 1] == '\n') why.resize(why.size() - 1);
                std::cerr << "shc: ignoring " << names[i] << ": " << why << "\n";
                continue;
            }
            Unit other;
            other.name = names[i];
            other.program = std::move(parsed);
            units.push_back(std::move(other));
            continue;
        }
        Unit first;
        first.name = names[0];
        first.program = std::move(parsed);
        units.insert(units.begin(), std::move(first));
    }

    std::unique_ptr<Program> program;
    if (!units.empty() && units[0].program) program = std::move(units[0].program);
    if (diagnostics.hasUnsupported()) {
        for (const Message &m : diagnostics.unsupportedItems()) {
            std::cerr << "shc: not compiled yet: " << m.text
                      << " (line " << m.line << ")\n";
        }
        return 3;
    }
    if (!program) {
        std::string text;
        diagnostics.writeTo(text);
        std::cout << text;
        return 1;
    }

    // Find the rest of the program. A call this file does not answer is
    // looked for in the others, and what is found is moved in - along with
    // whatever it calls in turn, and the globals of its own file that it
    // reads. Nothing arrives that was not asked for, which is why a file's
    // main() never does.
    if (program && units.size() > 1) {
        std::vector<Unit> others(std::make_move_iterator(units.begin() + 1),
                                 std::make_move_iterator(units.end()));
        Resolver resolver(diagnostics);
        if (!resolver.resolve(units[0] = Unit{names[0], std::move(program)}, others)) {
            std::string text;
            diagnostics.writeTo(text);
            std::cout << text;
            return 1;
        }
        program = std::move(units[0].program);

        // Said out loud, on standard error so it can never be mistaken for
        // the program's own output. A program that quietly grew a dependency
        // on another file is a program that will one day be moved without it.
        for (const std::string &from : resolver.used())
            std::cerr << "shc: also compiled " << from << "\n";
    }

    // Check. Unlike the two stages above it does not stop at the first
    // problem: it types the whole program, reports everything it finds, and
    // only then says whether the program may run. That is why several
    // messages can appear at once, and why warnings appear for programs that
    // run anyway.
    Checker checker(diagnostics);
    const bool sound = checker.check(*program);
    {
        std::string text;
        diagnostics.writeTo(text);
        std::cout << text;
    }
    // A construct the compiler has not reached yet is not a diagnostic about
    // the program. It says so in its own words, on standard error, with a
    // status of its own.
    if (diagnostics.hasUnsupported()) {
        for (const Message &m : diagnostics.unsupportedItems()) {
            std::cerr << "shc: not compiled yet: " << m.text
                      << " (line " << m.line << ")\n";
        }
        return 3;
    }
    if (!sound) return 1;

    std::unique_ptr<Emitter> emitter = target->newEmitter();
    CodeGen generator(*emitter);
    generator.run(*program, input_, names);

    // Whether -o was given, before the default hides it. -c needs to know:
    // with a name it is the object's, and without one the object is the
    // input's name with .o, which is what cc -c does.
    const bool named = !output_.empty();
    if (output_.empty()) output_ = stem(input_);

    const std::string assemblyPath =
        assemblyOnly_ ? output_ : output_ + target->assemblyExtension();
    if (!writeFile(assemblyPath, emitter->text())) {
        std::cerr << "shc: cannot write " << assemblyPath << "\n";
        return 2;
    }
    if (assemblyOnly_) return 0;

    if (!target->isHost()) {
        std::cerr << "shc: " << targetName_ << " assembly is in " << assemblyPath
                  << "; this host can only assemble " << Target::hostName() << "\n";
        return 2;
    }

    if (runtimeObject_.empty()) runtimeObject_ = defaultRuntimeObject(targetName_);

    // -o names the object exactly as it names the assembly under -S. It used
    // to have ".o" put on the end of it whatever it was, so `shc f.shm -c -o
    // f.o` wrote f.o.o - which is a nuisance by hand and fatal to anything
    // that has to name the object again to link it.
    std::string command;
    if (objectOnly_)
        command = "c++ -c -o " + (named ? output_ : output_ + ".o") + " " + assemblyPath;
    else
        command = "c++ -o " + output_ + " " + assemblyPath + " " + runtimeObject_;

    const int status = shell(command);
    std::remove(assemblyPath.c_str());
    return status == 0 ? 0 : 2;
}

}  // namespace shalimar
