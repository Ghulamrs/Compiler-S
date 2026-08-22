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
    // The other files the program may reach into. Named on the command line,
    // or found beside the program when none is named.
    std::vector<std::string> companions_;
    bool search_ = true;
    std::string output_;
    std::string targetName_;
    std::string runtimeObject_;
    std::string program_;          // argv[0], for finding the runtime beside it
    bool assemblyOnly_ = false;    // -S
    bool objectOnly_ = false;      // -c
    // Link the runtime that can be stopped and stepped. It changes
    // nothing about what is compiled - see docs/DEBUGGING.md.
    bool debug_ = false;

    // False when the arguments did not make sense; the reason has been said.
    bool parseArguments(const std::vector<std::string> &arguments);
    void usage() const;

    std::string defaultRuntimeObject(const std::string &targetName) const;
    static std::string stem(const std::string &path);
    // The file's own name, without the directories in front of it.
    static std::string leafOf(const std::string &path);
    static std::string directoryOf(const std::string &path);
    static bool readFile(const std::string &path, std::string &into);

    // The Shalimar files beside the program, which is what 'the project' means
    // to a compiler that was handed one file. Sorted, so that two runs of the
    // same compiler on the same directory agree about everything including
    // which file a clash is reported against.
    static std::vector<std::string> shalimarFilesIn(const std::string &directory);
    static bool looksLikeShalimar(const std::string &name);
    static bool writeFile(const std::string &path, const std::string &text);
    static int shell(const std::string &command);
};

}  // namespace shalimar
