// x86-64, COFF, Microsoft x64, written for ml64.
//
// MASM differs from the GNU assembler in more than syntax: it wants every
// symbol the module does not define declared with EXTRN before use, and a
// procedure opened with PROC and closed with ENDP. The externals are
// therefore not known until the last instruction has been emitted, so the
// body is built in the base class's buffer and the header is put in front of
// it in endModule().
#pragma once

#include "X86_64.h"

namespace shalimar {

class X86_64WindowsEmitter : public X86_64Emitter {
public:
    X86_64WindowsEmitter() : X86_64Emitter(spelling_impl_, microsoftAbi()) {}

    // x64 COFF puts no underscore in front of a C symbol; only the 32-bit
    // convention did.
    std::string symbol(const std::string &name) const override { return name; }

    void beginModule(const std::string &sourceName) override;
    void endModule() override;
    void beginFunction(const std::string &name, int slots) override;
    void endFunction() override;
    void label(int id) override;

private:
    // MASM has no temporary-symbol convention, so a label is an ordinary name
    // and the generator's numbering is what keeps it unique.
    std::string labelName(int id) const override { return "Lshm" + std::to_string(id); }

    MasmSpelling spelling_impl_;
    std::string sourceName_;
    std::string openProcedure_;
};

}  // namespace shalimar
