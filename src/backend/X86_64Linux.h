// x86-64, ELF, System V.
//
// GNU syntax and no underscore on a symbol. What is left after the Abi and
// the Spelling have taken their share is small, and that is the point: the
// difference between this and the Windows target is meant to be visible as a
// difference, not buried in two copies of the same instruction selection.
#pragma once

#include "X86_64.h"

namespace shalimar {

class X86_64LinuxEmitter : public X86_64Emitter {
public:
    X86_64LinuxEmitter() : X86_64Emitter(spelling_impl_, systemVAbi()) {}

    std::string symbol(const std::string &name) const override { return name; }

    void beginModule(const std::string &sourceName) override;
    void endModule() override;
    void beginFunction(const std::string &name, int slots) override;
    void endFunction() override;
    void label(int id) override;

private:
    // '.L' is what the GNU assembler treats as a temporary symbol on ELF.
    std::string labelName(int id) const override { return ".Lshm" + std::to_string(id); }

    GnuSpelling spelling_impl_;
};

}  // namespace shalimar
