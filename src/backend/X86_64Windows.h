
#pragma once

#include "X86_64.h"

namespace shalimar {

class X86_64WindowsEmitter : public X86_64Emitter {
public:
    X86_64WindowsEmitter() : X86_64Emitter(spelling_impl_, microsoftAbi()) {}

    std::string symbol(const std::string &name) const override { return name; }

    void beginModule(const std::string &sourceName) override;
    void endModule() override;
    void beginFunction(const std::string &name) override;
    void endFunction(int slots) override;
    void label(int id) override;

private:

    std::string labelName(int id) const override { return "Lshm" + std::to_string(id); }
    std::string bytesLabel(int id) const override { return "Lshmb" + std::to_string(id); }
    void openConstSection() override;
    void closeConstSection() override;
    void emitGlobalBlock(int slots) override;
    std::string globalsLabel() const override { return "shm_globals"; }

    MasmSpelling spelling_impl_;
    std::string sourceName_;
    std::string openProcedure_;
    int globalSlots_ = 0;
};

}
