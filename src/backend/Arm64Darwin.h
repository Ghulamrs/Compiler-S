
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

    static const int frameRecordBytes = 16;

    size_t prologueMark_ = 0;
    int frameBytes_ = 0;

    std::string slotAddress(int slot, int width);

    void adjustStack(const char *op, int bytes);

    void scratchConstant(uint64_t value);

    static char registerFile(Slot kind);
    static int byteWidth(Slot kind);
    std::string globalAddress(int index, int width);

    static std::string labelName(int id);
    static std::string bytesLabel(int id);

    void addressGlobals();

    void materialise(const std::string &reg, uint64_t bits, bool wide);
};

}
