// The x86-64 code, shared by both x86-64 targets.
//
// Three things vary between Linux and Windows here and they are separate
// axes. The Abi answers how arguments travel; the Spelling answers how an
// instruction is written down; the derived class answers what the assembler
// wants around the instructions - sections, symbol visibility, how a
// procedure is opened and closed. Instruction selection is none of those and
// lives here once.
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

    void loadInt(int32_t value) override;
    void setIntArg(int index) override;
    void callRuntime(const std::string &name) override;

protected:
    const Spelling &spelling_;
    const Abi &abi_;

    // rax is the integer accumulator, and also where a call's result lands.
    static const Reg accumulator = Reg::Ax;

    // Bytes the frame reserves below rbp. The shadow area lives at the bottom
    // of it, where a callee expects it, and the whole is rounded so that rsp
    // is sixteen-byte aligned at every call - which is what the ABI promises
    // and what an SSE spill in a callee depends on.
    int frameBytes() const;

    void emitPrologue();
    void emitEpilogue();

    // Names the module refers to but does not define. MASM wants each one
    // declared; the GNU assembler works them out for itself.
    void noteExternal(const std::string &name) override;
    const std::vector<std::string> &externals() const { return externals_; }

private:
    std::vector<std::string> externals_;
};

}  // namespace shalimar
