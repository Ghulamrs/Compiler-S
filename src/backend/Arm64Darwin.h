// arm64, Mach-O, Apple's ABI.
//
// The integer accumulator is w0 and the real accumulator will be d0, which
// are also the first argument and the return register - so a call whose only
// argument is the value just computed costs no move at all.
#pragma once

#include "Emitter.h"

namespace shalimar {

class Arm64DarwinEmitter : public Emitter {
public:
    std::string symbol(const std::string &name) const override { return "_" + name; }

    void beginModule(const std::string &sourceName) override;
    void endModule() override;

    void beginFunction(const std::string &name, int slots) override;
    void endFunction() override;

    void loadInt(int32_t value) override;
    void spillInt(int slot) override;
    void loadSlotIntoIntArg(int slot, int index) override;
    void setIntArg(int index) override;
    void callRuntime(const std::string &name) override;

private:
    // x29 and x30 saved at the top of the frame, everything else below it.
    static const int frameRecordBytes = 16;

    int frameBytes_ = 0;

    // Slot n sits below the frame record, at a negative offset from x29.
    std::string slotAddress(int slot) const;
};

}  // namespace shalimar
