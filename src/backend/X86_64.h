// The x86-64 code, shared by both x86-64 targets.
//
// Three things vary between Linux and Windows here and they are separate
// axes. The Abi answers how arguments travel; the Spelling answers how an
// instruction is written down; the derived class answers what the assembler
// wants around the instructions - sections, symbol visibility, how a
// procedure is opened and closed, and how a local label is named.
// Instruction selection is none of those and lives here once.
#pragma once

#include "Emitter.h"
#include "Spelling.h"

#include <string>
#include <vector>

namespace shalimar {

// A calling convention as data.
struct Abi {
    const Reg *intArgs;     // argument registers, in the order they fill
    int intArgCount;

    // Whether argument n takes slot n in whichever file, so that spending one
    // file's slot spends the other's. Microsoft's convention is positional
    // and System V's is not. No call the compiler emits mixes the two kinds
    // yet; when one does, this is the flag that decides it.
    bool positional;

    // The caller leaves this much room below the return address for the
    // callee to spill its register arguments into. Microsoft's ABI wants 32
    // bytes of it whether the callee uses them or not; System V wants none.
    int shadowBytes;
};

const Abi &systemVAbi();
const Abi &microsoftAbi();

class X86_64Emitter : public Emitter {
public:
    X86_64Emitter(const Spelling &spelling, const Abi &abi)
        : spelling_(spelling), abi_(abi) {}

    void loadIntConstant(int32_t value) override;
    void loadRealConstant(double value) override;

    void storeSlot(Slot kind, int slot) override;
    void loadSlot(Slot kind, int slot) override;
    void setArg(Slot kind, int index) override;
    void loadSlotIntoArg(Slot kind, int slot, int index) override;

    bool positionalArguments() const override { return abi_.positional; }
    void spillArgument(Slot kind, int registerIndex, int slot) override;
    void call(const std::string &name) override;
    void widenAccumulator() override;

    void loadSlotAddress(int slot) override;
    void storeThroughPointer(Slot kind, int pointerSlot) override;
    void loadThroughPointer(Slot kind, int pointerSlot) override;

    void defineBytes(int id, const std::string &bytes) override;
    void loadBytesAddress(int id) override;

    void jump(int id) override;
    void jumpIfZero(int id) override;

protected:
    const Spelling &spelling_;
    const Abi &abi_;

    // rax is the integer accumulator - eax when a value is an int, rax when
    // it is wide - and xmm0 is the real one. Both are also where a call's
    // result of that kind lands.
    static const Reg accumulator = Reg::Ax;
    static const Reg realAccumulator = Reg::Xmm0;

    // How a local label is spelled, which is the one piece of control flow
    // the two assemblers do not agree on.
    virtual std::string labelName(int id) const = 0;
    virtual std::string bytesLabel(int id) const = 0;

    // How a byte array is opened and closed, and how its address is taken.
    // ELF and COFF disagree about all three.
    virtual void openConstSection() = 0;
    virtual void closeConstSection() = 0;

    // Bytes the frame reserves below rbp: the slots first, then the shadow
    // area at the bottom where a callee expects to find it. The whole is
    // rounded so that rsp is sixteen-byte aligned at every call - which is
    // what the ABI promises and what an SSE spill in a callee depends on.
    void setSlots(int slots);
    int frameBytes() const { return frameBytes_; }

    std::string slotOperand(int slot, int width) const;

    void emitPrologue();
    void emitEpilogue();

    // Names the module refers to but does not define. MASM wants each one
    // declared; the GNU assembler works them out for itself.
    void noteExternal(const std::string &name) override;
    void noteDefined(const std::string &name);
    // Every name referred to and not defined here. A module that declares one
    // of its own definitions external is refused by MASM, and a call to a
    // function defined further down the same file is exactly that case.
    std::vector<std::string> externals() const;

private:
    std::vector<std::string> referenced_;
    std::vector<std::string> defined_;
    int frameBytes_ = 0;

    // Which register, which mnemonic and which width a slot's traffic uses.
    static Reg registerFor(Slot kind);
    static const char *moveFor(Slot kind);
    static int widthFor(Slot kind);
    static Reg sseArg(int index);
    Reg argRegister(Slot kind, int index) const;
};

}  // namespace shalimar
