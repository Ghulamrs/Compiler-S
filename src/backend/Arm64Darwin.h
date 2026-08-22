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

    void beginFunction(const std::string &name) override;
    void endFunction(int slots) override;

    void loadIntConstant(int32_t value) override;
    void loadRealConstant(double value) override;

    void storeSlot(Slot kind, int slot) override;
    void loadSlot(Slot kind, int slot) override;
    void setArg(Slot kind, int index) override;
    void loadSlotIntoArg(Slot kind, int slot, int index) override;

    bool positionalArguments() const override { return false; }
    int intArgCapacity() const override { return 8; }
    int realArgCapacity() const override { return 8; }
    void setOverflowBlock(int slot) override;
    void spillOverflowArgument(Slot kind, int index, int slot) override;
    void spillArgument(Slot kind, int registerIndex, int slot) override;
    void call(const std::string &name) override;
    void widenAccumulator() override;

    void loadSlotAddress(int slot) override;
    void storeThroughPointer(Slot kind, int pointerSlot) override;
    void loadThroughPointer(Slot kind, int pointerSlot) override;

    void defineGlobals(int slots) override;
    void loadGlobal(Slot kind, int index) override;
    void storeGlobal(Slot kind, int index) override;

    void defineBytes(int id, const std::string &bytes) override;
    void loadBytesAddress(int id) override;

    void label(int id) override;
    void jump(int id) override;
    void jumpIfZero(int id) override;

private:
    // x29 and x30 saved at the top of the frame, everything else below it.
    static const int frameRecordBytes = 16;

    size_t prologueMark_ = 0;
    int frameBytes_ = 0;

    // Slot n is at sp + 8n, counted upward. Upward from the stack pointer
    // rather than downward from the frame pointer, for two reasons: a
    // negative offset from x29 needs the unscaled form of ldr, which reaches
    // only -256, and an offset measured from the top of the frame would
    // depend on the frame's size - which is not known until the body has been
    // written.
    // The address of a slot, as an operand. A scaled load or store reaches
    // 4095 times its own width, so a slot far enough up a large frame does
    // not fit one - in which case the address is formed in x16 first and the
    // operand is that. x16 is the intra-procedure-call scratch register: it
    // belongs to the linker between calls and to us within one.
    std::string slotAddress(int slot, int width);

    // 'sub sp, sp, #n' takes twelve bits. A frame past 4095 bytes has its
    // size built in a register instead.
    void adjustStack(const char *op, int bytes);
    // A constant into x16, however wide.
    void scratchConstant(uint64_t value);

    // 'w', 'x' or 'd' - which register file and width a slot's traffic uses.
    static char registerFile(Slot kind);
    static int byteWidth(Slot kind);
    std::string globalAddress(int index, int width);
    // Mach-O treats a label beginning with 'L' as temporary, so it does not
    // reach the symbol table.
    static std::string labelName(int id);
    static std::string bytesLabel(int id);
    // The block's address into x9, which is caller-saved and holds nothing
    // of ours between instructions.
    void addressGlobals();

    // A 64-bit constant into a register, sixteen bits at a time. The
    // assembler's synthetic 'mov reg, #imm' would do this for a value it can
    // encode; writing the halves out means every value is reachable, which
    // matters most for the bit pattern of a double.
    void materialise(const std::string &reg, uint64_t bits, bool wide);
};

}  // namespace shalimar
