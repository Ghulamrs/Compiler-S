// arm64, Mach-O, Apple's ABI.
//
// The integer accumulator is w0/x0 and the real accumulator is d0, which are
// also the first argument and the return register - so a call whose only
// argument is the value just computed costs no move at all.
#pragma once

#include "Emitter.h"

namespace shalimar {

class Arm64DarwinEmitter : public Emitter {
public:
    std::string symbol(const std::string &name) const override { return "_" + name; }

    void beginModule(const std::string &sourceName) override;
    void endModule() override;

    void beginFunction(const std::string &name) override;
    void endFunction() override;

    void loadInt(int32_t value) override;
    void setIntArg(int index) override;
    void callRuntime(const std::string &name) override;

private:
    // Every frame here is the same shape: the frame record saved, x29 pointing
    // at it, and nothing else yet. Locals arrive with the feature that needs
    // them.
    static const int frameRecordBytes = 16;
};

}  // namespace shalimar
