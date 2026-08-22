#include "X86_64Windows.h"

namespace shalimar {

void X86_64WindowsEmitter::beginModule(const std::string &sourceName) {
    sourceName_ = sourceName;
    // Nothing goes out here. The EXTRN list has to precede the code and is
    // not complete until the code has been written, so the whole header is
    // put in front in endModule().
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
    for (const std::string &name : externals()) {
        header += "EXTRN\t" + name + ":PROC\n";
    }
    header += "\n_TEXT\tSEGMENT\n";

    text_ = header + text_;
    raw("_TEXT\tENDS");
    if (!data_.empty()) {
        blank();
        openConstSection();
        text_ += data_;
        closeConstSection();
    }
    raw("END");
}

}  // namespace shalimar
