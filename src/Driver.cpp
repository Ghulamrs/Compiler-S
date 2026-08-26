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
#include <limits.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include <vector>

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

#ifdef _WIN32
// ml64 and link are on PATH only inside a Developer Command Prompt, and an
// editor launched from Explorer is not one. Found once, then every command
// runs through a batch file that sources vcvars first - a file rather than a
// prefix because cmd's quote handling cannot be relied on with several
// quoted paths and an '&&'.
static std::string findVcvars() {
    char folder[MAX_PATH];
    char temp[MAX_PATH];
    if (GetTempPathA(MAX_PATH, folder) != 0 &&
        GetTempFileNameA(folder, "shc", 0, temp) != 0) {
        std::string ask =
            "\"\"C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe\""
            " -latest -products * -property installationPath > \"";
        ask += temp; ask += "\"\"";
        std::string root;
        if (std::system(ask.c_str()) == 0) {
            std::ifstream answer(temp);
            std::getline(answer, root);
        }
        std::remove(temp);
        while (!root.empty() && (root.back() == '\n' || root.back() == '\r')) root.pop_back();
        if (!root.empty()) {
            const std::string bat = root + "\\VC\\Auxiliary\\Build\\vcvars64.bat";
            std::ifstream there(bat.c_str());
            if (there) return bat;
        }
    }
    static const char *const roots[] = {
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2019\\"
    };
    static const char *const editions[] = {
        "Community", "Professional", "Enterprise", "BuildTools"
    };
    for (std::size_t r = 0; r < sizeof roots / sizeof roots[0]; ++r) {
        for (std::size_t e = 0; e < sizeof editions / sizeof editions[0]; ++e) {
            const std::string bat = std::string(roots[r]) + editions[e] +
                                    "\\VC\\Auxiliary\\Build\\vcvars64.bat";
            std::ifstream there(bat.c_str());
            if (there) return bat;
        }
    }
    return std::string();
}

static std::string developerShell() {
    const char *inside = std::getenv("VCToolsInstallDir");
    if (inside != nullptr && inside[0] != '\0') return std::string();
    static bool asked = false;
    static std::string cached;
    if (!asked) { asked = true; cached = findVcvars(); }
    return cached;
}
#endif

int Driver::shell(const std::string &command) {
#ifdef _WIN32
    // cmd /c strips the outer quotes of a command that begins and ends with
    // one, so it is given a pair of its own to eat.
    const std::string vcvars = developerShell();
    if (vcvars.empty()) return std::system(("\"" + command + "\"").c_str());

    char folder[MAX_PATH];
    char script[MAX_PATH];
    if (GetTempPathA(MAX_PATH, folder) == 0 ||
        GetTempFileNameA(folder, "shc", 0, script) == 0) {
        return std::system(("\"" + command + "\"").c_str());
    }
    std::string batch = script;
    std::remove(batch.c_str());
    batch += ".cmd";
    {
        std::ofstream out(batch.c_str());
        if (!out) return std::system(("\"" + command + "\"").c_str());
        out << "@echo off\n";
        out << "call \"" << vcvars << "\" >nul 2>&1\n";
        out << command << "\n";
    }
    const int rc = std::system(("\"" + batch + "\"").c_str());
    std::remove(batch.c_str());
    return rc;
#else
    return std::system(command.c_str());
#endif
}

static std::string shellQuote(const std::string &path) {
    return "\"" + path + "\"";
}

static bool exists(const std::string &path) {
    std::ifstream file(path.c_str());
    return file.good();
}

static std::string programDirectory(const std::string &argv0) {
    std::string full;

#if defined(_WIN32)
    char buffer[MAX_PATH];
    DWORD wrote = GetModuleFileNameA(NULL, buffer, sizeof buffer);
    if (wrote > 0 && wrote < sizeof buffer) full.assign(buffer, wrote);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    std::vector<char> buffer(size + 1, '\0');
    if (size != 0 && _NSGetExecutablePath(&buffer[0], &size) == 0)
        full = &buffer[0];
#else
    char buffer[4096];
    ssize_t wrote = readlink("/proc/self/exe", buffer, sizeof buffer - 1);
    if (wrote > 0) full.assign(buffer, static_cast<size_t>(wrote));
#endif

    if (full.empty()) full = argv0;

#ifndef _WIN32

    char resolved[PATH_MAX];
    if (realpath(full.c_str(), resolved) != NULL) full = resolved;
#endif

    size_t slash = full.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : full.substr(0, slash);
}

std::string Driver::defaultRuntimeObject(const std::string &targetName) const {
#ifdef _WIN32
    const char *extension = ".lib";
#else
    const char *extension = ".a";
#endif
    const std::string leaf =
        "/shmrt-" + targetName + (debug_ ? "-debug" : "") + extension;
    const std::string here = programDirectory(program_);

    const std::string beside = here + "/lib" + leaf;
    if (exists(beside)) return beside;

    const std::string installed = here + "/../lib" + leaf;
    if (exists(installed)) return installed;

    return beside;
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

        LexResult lexed = tokenize(source);
        if (lexed.failed) {

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

        for (const std::string &from : resolver.used())
            std::cerr << "shc: also compiled " << from << "\n";
    }

    Checker checker(diagnostics);
    const bool sound = checker.check(*program);
    {
        std::string text;
        diagnostics.writeTo(text);
        std::cout << text;
    }

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

    const bool named = !output_.empty();
    if (output_.empty()) {
        output_ = stem(input_);

#ifdef _WIN32
        if (!assemblyOnly_ && !objectOnly_) output_ += ".exe";
#endif
    }

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

#ifdef _WIN32

    const std::string objectPath =
        objectOnly_ ? (named ? output_ : output_ + ".obj") : output_ + ".obj";

    std::string command = "ml64 /nologo /c /Fo" + shellQuote(objectPath) + " " +
                          shellQuote(assemblyPath);
    int status = shell(command);
    if (status != 0) {
        std::cerr << "shc: the assembler failed - the command was:\n  " << command << "\n";
        noteWindowsToolchain();
        std::remove(assemblyPath.c_str());
        return 2;
    }
    std::remove(assemblyPath.c_str());
    if (objectOnly_) return 0;

    command = "link /nologo /subsystem:console /out:" + shellQuote(output_) + " " +
              shellQuote(objectPath) + " " + shellQuote(runtimeObject_);
    status = shell(command);

    std::remove(objectPath.c_str());
    if (status != 0) {
        std::cerr << "shc: the linker failed - the command was:\n  " << command << "\n";
        noteWindowsToolchain();
        return 2;
    }
    return 0;
#else
    std::string command;
    if (objectOnly_)
        command = "c++ -c -o " + shellQuote(named ? output_ : output_ + ".o") + " " +
                  shellQuote(assemblyPath);
    else
        // **-lm, named rather than relied upon.** A borrowed `sin` is now a
        // direct call to libm's own symbol, so the program needs libm whatever
        // the runtime archive happens to reference. It is inside libSystem on
        // a Mac and inside libc on glibc 2.34 and later, where the flag is a
        // no-op; on anything older it is the difference between linking and
        // "undefined reference to sin". Passing it always is one word and
        // removes a fault that would pass here and on the build box and fail
        // on somebody else's machine. See docs/FOREIGN.md.
        command = "c++ -o " + shellQuote(output_) + " " + shellQuote(assemblyPath) + " " +
                  shellQuote(runtimeObject_) + " -lm";

    const int status = shell(command);
    std::remove(assemblyPath.c_str());
    return status == 0 ? 0 : 2;
#endif
}

void Driver::noteWindowsToolchain() {
#ifdef _WIN32
    std::cerr <<
        "shc: ml64 and link ship with Visual Studio and are on PATH only inside\n"
        "     a Developer Command Prompt, or after vcvars64.bat has been run.\n";
#endif
}

}
