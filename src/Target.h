// A target machine.
//
// Selecting one is the only place the compiler asks which machine it is
// writing for. Everything downstream holds an Emitter and does not know.
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class Emitter;

class Target {
public:
    virtual ~Target() = default;

    // Null for a name that is not one of names().
    static std::unique_ptr<Target> forName(const std::string &name);

    // The target this compiler is running on, which is the only one it can
    // assemble and link without help.
    static std::string hostName();
    static std::vector<std::string> names();

    virtual std::string name() const = 0;
    virtual std::unique_ptr<Emitter> newEmitter() const = 0;

    // '.s' for an assembler that reads GNU syntax, '.asm' for ml64 - which
    // takes the extension as part of how it is invoked, not as decoration.
    virtual std::string assemblyExtension() const { return ".s"; }

    bool isHost() const { return name() == hostName(); }
};

}  // namespace shalimar
