
#pragma once

#include <cstdint>
#include <string>

namespace shalimar {

enum class Slot { Int, Real, Wide };

class Emitter {
public:
    virtual ~Emitter() = default;

    const std::string &text() const { return text_; }

    virtual std::string symbol(const std::string &name) const = 0;

    virtual void beginModule(const std::string &sourceName) = 0;
    virtual void endModule() = 0;

    virtual void beginFunction(const std::string &name) = 0;
    virtual void endFunction(int slots) = 0;

    virtual void loadIntConstant(int32_t value) = 0;
    virtual void loadRealConstant(double value) = 0;

    virtual void storeSlot(Slot kind, int slot) = 0;
    virtual void loadSlot(Slot kind, int slot) = 0;

    virtual void setArg(Slot kind, int index) = 0;
    virtual void loadSlotIntoArg(Slot kind, int slot, int index) = 0;

    virtual bool positionalArguments() const = 0;

    virtual int intArgCapacity() const = 0;
    virtual int realArgCapacity() const = 0;

    virtual void setOverflowBlock(int slot) = 0;
    virtual void spillOverflowArgument(Slot kind, int index, int slot) = 0;

    virtual void spillArgument(Slot kind, int registerIndex, int slot) = 0;

    virtual void call(const std::string &name) = 0;

    virtual void loadSlotAddress(int slot) = 0;
    virtual void storeThroughPointer(Slot kind, int pointerSlot) = 0;
    virtual void loadThroughPointer(Slot kind, int pointerSlot) = 0;

    virtual void defineGlobals(int slots) = 0;
    virtual void loadGlobal(Slot kind, int index) = 0;
    virtual void storeGlobal(Slot kind, int index) = 0;

    virtual void defineBytes(int id, const std::string &bytes) = 0;
    virtual void loadBytesAddress(int id) = 0;

    virtual void widenAccumulator() = 0;

    virtual void label(int id) = 0;
    virtual void jump(int id) = 0;

    virtual void jumpIfZero(int id) = 0;

    void setLine(int unit, int line) {
        loadIntConstant(line);
        setArg(Slot::Int, 1);
        loadIntConstant(unit);
        setArg(Slot::Int, 0);
        call("shm_line");
    }

protected:

    void instruction(const std::string &line) { text_ += "\t" + line + "\n"; }

    void raw(const std::string &line) { text_ += line + "\n"; }
    void blank() { text_ += "\n"; }

    virtual void noteExternal(const std::string &) {}

    static uint64_t bitsOf(double value);

    std::string data_;

    std::string text_;
};

}
