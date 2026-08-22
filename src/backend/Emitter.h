// What a target can be asked to do.
//
// The code generator walks the tree once and speaks only to this interface,
// so a target is a class deriving from it rather than a second walk of the
// program. Everything a target may differ over is a method here; anything
// that is the same for all three - the order values are evaluated in, where a
// jump goes, which runtime entry point a type reaches - stays in the
// generator and is written once.
//
// The machine model is deliberately small. There is one accumulator per kind
// of value, and anything that has to outlive the evaluation of something else
// is parked in a numbered slot in the current frame. A slot is eight bytes
// and holds any kind. That is slower than a register allocator and it is the
// same shape on all three machines, which is what lets one generator drive
// them.
#pragma once

#include <cstdint>
#include <string>

namespace shalimar {

// What a slot holds, and therefore which accumulator and which width the
// traffic to and from it uses.
//
// Wide is a 64-bit integer. Nothing in Shalimar is one - int is 32 bits - but
// the compiler needs them: a loop's hidden pass counter is wide so that
// stepping it cannot overflow where the language says the loop should simply
// end, and an array will travel as a pointer.
enum class Slot { Int, Real, Wide };

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

    virtual void loadIntConstant(int32_t value) = 0;
    virtual void loadRealConstant(double value) = 0;

    // Park an accumulator, read one back. Between them these are the whole of
    // how a value survives the evaluation of another, and also how a named
    // variable is read and written.
    virtual void storeSlot(Slot kind, int slot) = 0;
    virtual void loadSlot(Slot kind, int slot) = 0;

    // Argument setup. `index` counts within its own file: the first real
    // argument is real index 0 whether or not an int precedes it. That is
    // System V's rule and Apple's; Microsoft's is positional, and the day a
    // call mixes the two kinds this interface has to ask the Abi. Nothing
    // calls mixed yet, and this comment is here so that the day it does, the
    // reason is not rediscovered.
    virtual void setArg(Slot kind, int index) = 0;
    virtual void loadSlotIntoArg(Slot kind, int slot, int index) = 0;

    // Whether argument n takes slot n in whichever register file, so that
    // spending one file's slot spends the other's. Microsoft's convention is
    // positional; System V's and Apple's are not. The generator asks because
    // it is the one that knows a call's signature.
    virtual bool positionalArguments() const = 0;

    // A parameter arriving in a register, put where the body will look for
    // it. Called once per parameter, immediately after beginFunction.
    virtual void spillArgument(Slot kind, int registerIndex, int slot) = 0;

    // The result lands in the accumulator of whichever kind the callee
    // returns; the generator knows which and the target does not have to.
    // The name is the linker's, before the target's own prefix.
    virtual void call(const std::string &name) = 0;

    // Addresses. A slot's own address is how a '&' parameter and a second
    // output travel; reading and writing through one is what the other end
    // of that does.
    virtual void loadSlotAddress(int slot) = 0;
    virtual void storeThroughPointer(Slot kind, int pointerSlot) = 0;
    virtual void loadThroughPointer(Slot kind, int pointerSlot) = 0;

    // Read-only bytes, named by a number the generator hands out. Written
    // numerically rather than as a quoted string: the two assemblers escape
    // differently and neither escapes everything, and a byte list has no
    // escaping to get wrong.
    virtual void defineBytes(int id, const std::string &bytes) = 0;
    virtual void loadBytesAddress(int id) = 0;

    // Sign-extend the int accumulator into the wide one. They are the same
    // register on both architectures, which is why this is a widening rather
    // than a move.
    virtual void widenAccumulator() = 0;

    // Control flow. A label is named by a number the generator hands out, so
    // the three targets need agree on nothing but that it is unique.
    virtual void label(int id) = 0;
    virtual void jump(int id) = 0;
    // Branches on the int accumulator being zero, which is what a condition
    // has been reduced to by the time it gets here.
    virtual void jumpIfZero(int id) = 0;

    // Tell the runtime which statement is executing, so that a failure names
    // it. Written as a call rather than a store to a global because a global
    // has to be addressed, and addressing one is three different spellings.
    void setLine(int line) {
        loadIntConstant(line);
        setArg(Slot::Int, 0);
        call("shm_line");
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

    // Read-only data, built up as the code is written and put out by the
    // target when the module ends - which is also when MASM's EXTRN list is
    // known, so the two arrive together rather than needing two passes.
    std::string data_;

    std::string text_;
};

}  // namespace shalimar
