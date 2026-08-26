#include "X86_64Windows.h"

namespace shalimar {

void X86_64WindowsEmitter::beginModule(const std::string &sourceName) {
    sourceName_ = sourceName;

}

void X86_64WindowsEmitter::beginFunction(const std::string &name) {
    const std::string s = symbol(name);
    noteDefined(s);
    openProcedure_ = s;
    blank();
    raw("PUBLIC\t" + s);
    raw(s + "\tPROC");
    prologueMark_ = text_.size();
}

void X86_64WindowsEmitter::endFunction(int slots) {
    emitEpilogue(slots);
    raw(openProcedure_ + "\tENDP");
    openProcedure_.clear();
    text_.insert(prologueMark_, prologue(slots));
}

void X86_64WindowsEmitter::label(int id) {
    raw(labelName(id) + ":");
}

void X86_64WindowsEmitter::emitGlobalBlock(int slots) {
    globalSlots_ = slots;
}

void X86_64WindowsEmitter::openConstSection() {
    raw("CONST\tSEGMENT");
}

void X86_64WindowsEmitter::closeConstSection() {
    raw("CONST\tENDS");
}

void X86_64WindowsEmitter::endModule() {
    std::string header;
    header += "; " + sourceName_ + "\n";
    header += "OPTION\tCASEMAP:NONE\n";

    // **A C library function can share its name with an x87 instruction.**
    // Borrowing `abs` means calling libm's `fabs`, and FABS is a mnemonic ml64
    // has known since the 8087 - so `EXTRN fabs:PROC` is read as an
    // instruction and answers `A2008: syntax error : fabs`. The other sixteen
    // borrowable names are clear; it is only the ones that begin with `f`,
    // and of those only this one exists in libm under a name we emit.
    //
    // OPTION NOKEYWORD is MASM's own answer, and it is scoped to the module
    // rather than the symbol - which is safe here because this compiler emits
    // no x87 at all. Every float goes through SSE.
    //
    // Listed from the externals actually used, so a module that never calls
    // fabs does not carry the directive, and a name added to the table later
    // gets the same treatment by appearing in this list.
    std::string suppressed;
    for (const std::string &name : externals()) {
        if (name == "fabs") suppressed += (suppressed.empty() ? "" : " ") + name;
    }
    if (!suppressed.empty()) {
        header += "OPTION\tNOKEYWORD:<" + suppressed + ">\n";
    }

    for (const std::string &name : externals()) {
        header += "EXTRN\t" + name + ":PROC\n";
    }
    header += "\n_TEXT\tSEGMENT\n";

    text_ = header + text_;
    raw("_TEXT\tENDS");
    if (globalSlots_ > 0) {
        blank();
        raw("_BSS\tSEGMENT");
        raw(globalsLabel() + "\tQWORD\t" + std::to_string(globalSlots_) + " DUP (0)");
        raw("_BSS\tENDS");
    }
    if (!data_.empty()) {
        blank();
        openConstSection();
        text_ += data_;
        closeConstSection();
    }
    raw("END");
}

}
