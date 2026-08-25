
#pragma once

#include "Emitter.h"
#include "Spelling.h"

#include <string>
#include <vector>

namespace shalimar {

struct Abi {
    const Reg *intArgs;
    int intArgCount;
    int sseArgCount;

    bool positional;

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
    int intArgCapacity() const override { return abi_.intArgCount; }
    int realArgCapacity() const override { return abi_.sseArgCount; }
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

    void jump(int id) override;
    void jumpIfZero(int id) override;

protected:
    const Spelling &spelling_;
    const Abi &abi_;

    static const Reg accumulator = Reg::Ax;
    static const Reg realAccumulator = Reg::Xmm0;

    static const Reg overflowPointer = Reg::R10;

    virtual std::string labelName(int id) const = 0;
    virtual std::string bytesLabel(int id) const = 0;

    virtual void openConstSection() = 0;
    virtual void closeConstSection() = 0;

    virtual void emitGlobalBlock(int slots) = 0;
    virtual std::string globalsLabel() const = 0;

    std::string slotOperand(int slot, int width) const;

    std::string prologue(int slots);
    void emitEpilogue(int slots);
    int frameBytesFor(int slots) const;

    size_t prologueMark_ = 0;

    void noteExternal(const std::string &name) override;
    void noteDefined(const std::string &name);

    std::vector<std::string> externals() const;

private:
    std::vector<std::string> referenced_;
    std::vector<std::string> defined_;

    static Reg registerFor(Slot kind);
    static const char *moveFor(Slot kind);
    static int widthFor(Slot kind);
    static Reg sseArg(int index);
    Reg argRegister(Slot kind, int index) const;
};

}
