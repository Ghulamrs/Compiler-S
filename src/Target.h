
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace shalimar {

class Emitter;

class Target {
public:
    virtual ~Target() = default;

    static std::unique_ptr<Target> forName(const std::string &name);

    static std::string hostName();
    static std::vector<std::string> names();

    virtual std::string name() const = 0;
    virtual std::unique_ptr<Emitter> newEmitter() const = 0;

    virtual std::string assemblyExtension() const { return ".s"; }

    bool isHost() const { return name() == hostName(); }
};

}
