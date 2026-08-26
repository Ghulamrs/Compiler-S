
#pragma once

#include <string>
#include <vector>

namespace shalimar {

class Driver {
public:
    int run(const std::vector<std::string> &arguments);

private:
    std::string input_;

    std::vector<std::string> companions_;
    bool search_ = true;
    std::string output_;
    std::string targetName_;
    std::string runtimeObject_;
    // Libraries named with --with=, in the order given, holding what `uses
    // <...> = f(...)` declared. See docs/FOREIGN.md.
    std::vector<std::string> libraries_;

    // Set when --version or --help was answered, so run() can leave with 0
    // rather than reporting a usage error the caller did not make.
    bool answered_ = false;
    std::string program_;
    bool assemblyOnly_ = false;
    bool objectOnly_ = false;

    bool debug_ = false;

    bool parseArguments(const std::vector<std::string> &arguments);
    void usage() const;

    std::string defaultRuntimeObject(const std::string &targetName) const;

    static void noteWindowsToolchain();
    static std::string stem(const std::string &path);

    static std::string leafOf(const std::string &path);
    static std::string directoryOf(const std::string &path);
    static bool readFile(const std::string &path, std::string &into);

    static std::vector<std::string> shalimarFilesIn(const std::string &directory);
    static bool looksLikeShalimar(const std::string &name);
    static bool writeFile(const std::string &path, const std::string &text);
    static int shell(const std::string &command);
};

}
