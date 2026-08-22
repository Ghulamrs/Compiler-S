// The driver: command line in, exit status out.
//
// It owns the order of the stages and nothing else. That order mirrors the
// app's - lex, stop on a lex error, parse, stop on a parse error, check and
// report everything, then emit - so that a program the app refuses is a
// program this refuses, for the same reason and on the same line.
#pragma once

#include <string>
#include <vector>

namespace shalimar {

class Driver {
public:
    int run(const std::vector<std::string> &arguments);

private:
    std::string input_;
    std::string output_;
    std::string targetName_;
    std::string runtimeObject_;
    std::string program_;          // argv[0], for finding the runtime beside it
    bool assemblyOnly_ = false;    // -S
    bool objectOnly_ = false;      // -c

    // False when the arguments did not make sense; the reason has been said.
    bool parseArguments(const std::vector<std::string> &arguments);
    void usage() const;

    std::string defaultRuntimeObject(const std::string &targetName) const;
    static std::string stem(const std::string &path);
    static std::string directoryOf(const std::string &path);
    static bool readFile(const std::string &path, std::string &into);
    static bool writeFile(const std::string &path, const std::string &text);
    static int shell(const std::string &command);
};

}  // namespace shalimar
