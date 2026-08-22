#include "Driver.h"

#include "CodeGen.h"
#include "Diag.h"
#include "Parser.h"
#include "Target.h"
#include "Token.h"
#include "backend/Emitter.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace shalimar {

void Driver::usage() const {
    std::cerr <<
        "usage: shc [options] file.shm\n"
        "\n"
        "  -o <path>          where the result goes\n"
        "  -S                 stop after writing assembly\n"
        "  -c                 stop after assembling\n"
        "  --target=<name>    arm64-darwin | x86_64-linux | x86_64-windows\n"
        "  --runtime=<path>   the runtime object to link against\n";
}

std::string Driver::stem(const std::string &path) {
    size_t slash = path.find_last_of("/\\");
    std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    return dot == std::string::npos ? base : base.substr(0, dot);
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

int Driver::shell(const std::string &command) {
    return std::system(command.c_str());
}

std::string Driver::defaultRuntimeObject(const std::string &targetName) const {
    return directoryOf(program_) + "/lib/shmrt-" + targetName + ".o";
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
        } else if (a == "-h" || a == "--help") {
            usage();
            return false;
        } else if (!a.empty() && a[0] == '-') {
            std::cerr << "shc: unknown option " << a << "\n";
            return false;
        } else if (input_.empty()) {
            input_ = a;
        } else {
            std::cerr << "shc: one program at a time\n";
            return false;
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

    std::string source;
    if (!readFile(input_, source)) {
        std::cerr << "shc: cannot read " << input_ << "\n";
        return 2;
    }

    // Lex. Tokenizing stops at the offending character, so nothing downstream
    // is trustworthy and the error is reported alone.
    LexResult lexed = tokenize(source);
    if (lexed.failed) {
        Diagnostics diagnostics;
        diagnostics.error(lexed.errorLine, lexed.error);
        std::string text;
        diagnostics.writeTo(text);
        std::cout << text;
        return 1;
    }

    // Parse. Reported one at a time; parsing stops at the first.
    Diagnostics diagnostics;
    Parser parser(lexed.tokens, diagnostics);
    std::unique_ptr<Program> program = parser.parse();
    if (!program) {
        std::string text;
        diagnostics.writeTo(text);
        std::cout << text;
        return 1;
    }

    std::unique_ptr<Emitter> emitter = target->newEmitter();
    CodeGen generator(*emitter);
    generator.run(*program, input_);

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

    std::string command = "c++ -o " + output_ + " " + assemblyPath;
    if (!objectOnly_) command += " " + runtimeObject_;
    else command = "c++ -c -o " + output_ + ".o " + assemblyPath;

    const int status = shell(command);
    std::remove(assemblyPath.c_str());
    return status == 0 ? 0 : 2;
}

}  // namespace shalimar
