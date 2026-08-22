// What a target can be asked to do.
//
// The code generator walks the tree once and speaks only to this interface,
// so a target is a class deriving from it rather than a second walk of the
// program. Everything a target may differ over is a method here; anything
// that is the same for all three - the order values are evaluated in, where a
// jump goes, which runtime entry point a type reaches - stays in the
// generator and is written once.
//
// The machine model is deliberately small. There are two accumulators, one
// integer and one real, and an operand that has to outlive the evaluation of
// its sibling is parked in a numbered slot in the current frame. A slot is
// eight bytes and holds either kind. That is slower than a register allocator
// and it is the same shape on all three machines, which is what lets one
// generator drive them.
#pragma once

#include <cstdint>
#include <string>

namespace shalimar {

class Emitter {
public:
    virtual ~Emitter() = default;

    // The finished assembly. Valid after endModule().
    const std::string &text() const { return text_; }

    // How the target spells a name that has to be visible to the linker.
    // Mach-O puts an underscore in front of every C symbol; ELF and COFF-x64
    // do not.
    virtual std::string symbol(const std::string &name) const = 0;

    virtual void beginModule(const std::string &sourceName) = 0;
    virtual void endModule() = 0;

    // `slots` is how many eight-byte places the frame must hold. The checker
    // worked it out while it typed the function.
    virtual void beginFunction(const std::string &name, int slots) = 0;
    virtual void endFunction() = 0;

    // Load a constant into the accumulator of its kind.
    virtual void loadIntConstant(int32_t value) = 0;
    virtual void loadRealConstant(double value) = 0;

    // The frame slots: park an accumulator, read one back. Between them these
    // are the whole of how an operand survives the evaluation of its sibling,
    // and also how a named variable is read and written.
    virtual void storeIntSlot(int slot) = 0;
    virtual void loadIntSlot(int slot) = 0;
    virtual void storeRealSlot(int slot) = 0;
    virtual void loadRealSlot(int slot) = 0;

    // Argument setup. `index` counts within its own kind: the first real
    // argument is real index 0 whether or not an int precedes it. That is
    // System V's rule and Apple's; Microsoft's is positional, and the day a
    // call mixes the two kinds this interface has to grow an Abi question.
    // Nothing calls mixed yet, and this comment is here so that the day it
    // does, the reason is not rediscovered.
    virtual void setIntArg(int index) = 0;
    virtual void setRealArg(int index) = 0;
    virtual void loadIntSlotIntoArg(int slot, int index) = 0;
    virtual void loadRealSlotIntoArg(int slot, int index) = 0;

    // The result lands in the accumulator of whichever kind the callee
    // returns; the generator knows which and the target does not have to.
    virtual void callRuntime(const std::string &name) = 0;

    // Tell the runtime which statement is executing, so that a failure names
    // it. Written as a call rather than a store to a global because a global
    // has to be addressed, and addressing one is three different spellings.
    void setLine(int line) {
        loadIntConstant(line);
        setIntArg(0);
        callRuntime("shm_line");
    }

protected:
    // Append one line of assembly, indented as an instruction.
    void instruction(const std::string &line) { text_ += "\t" + line + "\n"; }
    // Append one line with no indent: a label, a directive that owns its line.
    void raw(const std::string &line) { text_ += line + "\n"; }
    void blank() { text_ += "\n"; }

    // Names a target has to declare before use - MASM wants an EXTRN for
    // every symbol it does not define. Recorded here so a target that needs
    // the list has it and one that does not can ignore it.
    virtual void noteExternal(const std::string &) {}

    // The bits of a double, which is how a real constant is materialised:
    // built in an integer register and moved across. That costs one extra
    // instruction and saves a constant pool, a section and a relocation on
    // every target at once.
    static uint64_t bitsOf(double value);

    std::string text_;
};

}  // namespace shalimar
