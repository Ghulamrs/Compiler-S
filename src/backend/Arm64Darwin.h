// arm64, Mach-O, Apple's ABI.
//
// The integer accumulator is w0 (x0 when wide) and the real accumulator is
// d0, which are also the first argument and the return register of each kind
// - so a call whose only argument is the value just computed costs no move at
// all.
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

    void storeSlot(Slot kind, int slot) override;
    void loadSlot(Slot kind, int slot) override;
    void setArg(Slot kind, int index) override;
    void loadSlotIntoArg(Slot kind, int slot, int index) override;

    bool positionalArguments() const override { return false; }
    void spillArgument(Slot kind, int registerIndex, int slot) override;
    void call(const std::string &name) override;
    void widenAccumulator() override;

    void loadSlotAddress(int slot) override;
    void storeThroughPointer(Slot kind, int pointerSlot) override;
    void loadThroughPointer(Slot kind, int pointerSlot) override;

    void defineBytes(int id, const std::string &bytes) override;
    void loadBytesAddress(int id) override;

    void label(int id) override;
    void jump(int id) override;
    void jumpIfZero(int id) override;

private:
    // x29 and x30 saved at the top of the frame, everything else below it.
    static const int frameRecordBytes = 16;

    int frameBytes_ = 0;

    // Slot n sits below the frame record, at a negative offset from x29.
    std::string slotAddress(int slot) const;

    // 'w', 'x' or 'd' - which register file and width a slot's traffic uses.
    static char registerFile(Slot kind);
    // Mach-O treats a label beginning with 'L' as temporary, so it does not
    // reach the symbol table.
    static std::string labelName(int id);
    static std::string bytesLabel(int id);

    // A 64-bit constant into a register, sixteen bits at a time. The
    // assembler's synthetic 'mov reg, #imm' would do this for a value it can
    // encode; writing the halves out means every value is reachable, which
    // matters most for the bit pattern of a double.
    void materialise(const std::string &reg, uint64_t bits, bool wide);
};

}  // namespace shalimar
