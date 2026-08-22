#include "X86_64Linux.h"

namespace shalimar {

void X86_64LinuxEmitter::beginModule(const std::string &sourceName) {
    raw("# " + sourceName);
    raw("\t.text");
}

void X86_64LinuxEmitter::openConstSection() {
    raw("\t.section\t.rodata");
}

void X86_64LinuxEmitter::closeConstSection() {
    raw("\t.text");
}

void X86_64LinuxEmitter::endModule() {
    if (!data_.empty()) {
        blank();
        openConstSection();
        text_ += data_;
        closeConstSection();
    }
    // A GNU-as object gets a non-executable stack only if it says so, and a
    // missing note makes the whole program's stack executable on modern
    // toolchains. Nothing here needs one.
    blank();
    raw("\t.section\t.note.GNU-stack,\"\",@progbits");
}

void X86_64LinuxEmitter::beginFunction(const std::string &name) {
    const std::string s = symbol(name);
    blank();
    raw("\t.globl\t" + s);
    raw("\t.type\t" + s + ", @function");
    raw(s + ":");
    prologueMark_ = text_.size();
}

void X86_64LinuxEmitter::endFunction(int slots) {
    emitEpilogue(slots);
    text_.insert(prologueMark_, prologue(slots));
}

void X86_64LinuxEmitter::label(int id) {
    raw(labelName(id) + ":");
}

}  // namespace shalimar
