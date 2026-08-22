// arm64, Mach-O, Apple's ABI.
//
// The integer accumulator is w0 and the real accumulator is d0, which are
// also the first argument and the return register of each kind - so a call
// whose only argument is the value just computed costs no move at all.
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

    void loadIntConstant(int32_t value) override;
    void loadRealConstant(double value) override;

    void storeIntSlot(int slot) override;
    void loadIntSlot(int slot) override;
    void storeRealSlot(int slot) override;
    void loadRealSlot(int slot) override;

    void setIntArg(int index) override;
    void setRealArg(int index) override;
    void loadIntSlotIntoArg(int slot, int index) override;
    void loadRealSlotIntoArg(int slot, int index) override;

    void callRuntime(const std::string &name) override;

private:
    // x29 and x30 saved at the top of the frame, everything else below it.
    static const int frameRecordBytes = 16;

    int frameBytes_ = 0;

    // Slot n sits below the frame record, at a negative offset from x29.
    std::string slotAddress(int slot) const;

    // A 64-bit constant into a register, sixteen bits at a time. The
    // assembler's synthetic 'mov reg, #imm' would do this for a value it can
    // encode; writing the halves out means every value is reachable, which
    // matters most for the bit pattern of a double.
    void materialise(const std::string &reg, uint64_t bits, bool wide);
};

}  // namespace shalimar
