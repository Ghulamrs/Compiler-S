// What a target can be asked to do.
//
// The code generator walks the tree once and speaks only to this interface,
// so a target is a class deriving from it rather than a second walk of the
// program. Everything a target may differ over is a method here; anything
// that is the same for all three - the order values are evaluated in, where a
// jump goes, what a print item costs - stays in the generator and is written
// once.
//
// The machine model is deliberately small. Expressions evaluate into one
// integer accumulator and one real accumulator, and an operand that has to
// outlive the evaluation of its sibling is parked in a numbered slot in the
// current frame. That is slower than a register allocator and it is the same
// shape on all three machines, which is what lets one generator drive them.
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

    // Integer accumulator.
    virtual void loadInt(int32_t value) = 0;

    // Park the accumulator in a numbered frame slot, and read one back into
    // an argument register. Between them these are the whole of how an
    // operand survives the evaluation of its sibling.
    virtual void spillInt(int slot) = 0;
    virtual void loadSlotIntoIntArg(int slot, int index) = 0;

    // Move the integer accumulator into the register argument `index` takes,
    // then call. Arguments are set in descending index order, so that a
    // target which passes the first argument in the accumulator's own
    // register does not overwrite the accumulator before reading it.
    virtual void setIntArg(int index) = 0;
    virtual void callRuntime(const std::string &name) = 0;

    // Tell the runtime which statement is executing, so that a failure names
    // it. Written as a call rather than a store to a global because a global
    // has to be addressed, and addressing one is three different spellings.
    void setLine(int line) {
        loadInt(line);
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

    std::string text_;
};

}  // namespace shalimar
