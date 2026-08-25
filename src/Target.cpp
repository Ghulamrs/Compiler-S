#include "Target.h"

#include "backend/Arm64Darwin.h"
#include "backend/X86_64Linux.h"
#include "backend/X86_64Windows.h"

namespace shalimar {
namespace {

class Arm64DarwinTarget : public Target {
public:
    std::string name() const override { return "arm64-darwin"; }
    std::unique_ptr<Emitter> newEmitter() const override {
        return std::unique_ptr<Emitter>(new Arm64DarwinEmitter());
    }
};

class X86_64LinuxTarget : public Target {
public:
    std::string name() const override { return "x86_64-linux"; }
    std::unique_ptr<Emitter> newEmitter() const override {
        return std::unique_ptr<Emitter>(new X86_64LinuxEmitter());
    }
};

class X86_64WindowsTarget : public Target {
public:
    std::string name() const override { return "x86_64-windows"; }
    std::string assemblyExtension() const override { return ".asm"; }
    std::unique_ptr<Emitter> newEmitter() const override {
        return std::unique_ptr<Emitter>(new X86_64WindowsEmitter());
    }
};

}

std::vector<std::string> Target::names() {
    return {"arm64-darwin", "x86_64-linux", "x86_64-windows"};
}

std::string Target::hostName() {
#if defined(_WIN32)
    return "x86_64-windows";
#elif defined(__APPLE__)
    return "arm64-darwin";
#else
    return "x86_64-linux";
#endif
}

std::unique_ptr<Target> Target::forName(const std::string &name) {
    if (name == "arm64-darwin")   return std::unique_ptr<Target>(new Arm64DarwinTarget());
    if (name == "x86_64-linux")   return std::unique_ptr<Target>(new X86_64LinuxTarget());
    if (name == "x86_64-windows") return std::unique_ptr<Target>(new X86_64WindowsTarget());
    return nullptr;
}

}
