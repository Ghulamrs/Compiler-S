#include "Arm64Darwin.h"

namespace shalimar {

void Arm64DarwinEmitter::beginModule(const std::string &sourceName) {
    raw("// " + sourceName);
    raw("\t.section\t__TEXT,__text,regular,pure_instructions");
}

void Arm64DarwinEmitter::endModule() {
    if (!data_.empty()) {
        blank();
        raw("\t.section\t__TEXT,__const");
        text_ += data_;
    }
    blank();
}

std::string Arm64DarwinEmitter::slotAddress(int slot) {
    return "[sp, #" + std::to_string(8 * slot) + "]";
}

char Arm64DarwinEmitter::registerFile(Slot kind) {
    switch (kind) {
    case Slot::Int:  return 'w';
    case Slot::Real: return 'd';
    case Slot::Wide: return 'x';
    }
    return 'w';
}

std::string Arm64DarwinEmitter::labelName(int id) {
    return "Lshm" + std::to_string(id);
}

void Arm64DarwinEmitter::beginFunction(const std::string &name) {
    blank();
    raw("\t.globl\t" + symbol(name));
    raw("\t.p2align\t2");
    raw(symbol(name) + ":");
    prologueMark_ = text_.size();
}

void Arm64DarwinEmitter::endFunction(int slots) {
    // The stack pointer must stay sixteen-byte aligned, so the reservation is
    // rounded up rather than taken as it comes.
    frameBytes_ = (slots * 8 + 15) & ~15;

    if (frameBytes_ > 0) instruction("add\tsp, sp, #" + std::to_string(frameBytes_));
    instruction("ldp\tx29, x30, [sp], #" + std::to_string(frameRecordBytes));
    instruction("ret");

    // stp with a pre-index writes the pair and moves sp in one instruction,
    // and sixteen bytes is what the alignment wanted anyway.
    std::string prologue;
    prologue += "\tstp\tx29, x30, [sp, #-" + std::to_string(frameRecordBytes) + "]!\n";
    prologue += "\tmov\tx29, sp\n";
    if (frameBytes_ > 0) prologue += "\tsub\tsp, sp, #" + std::to_string(frameBytes_) + "\n";
    text_.insert(prologueMark_, prologue);
}

void Arm64DarwinEmitter::materialise(const std::string &reg, uint64_t bits, bool wide) {
    const int chunks = wide ? 4 : 2;
    instruction("movz\t" + reg + ", #" + std::to_string(bits & 0xFFFFu));
    for (int i = 1; i < chunks; ++i) {
        const uint64_t part = (bits >> (16 * i)) & 0xFFFFu;
        if (part == 0) continue;
        instruction("movk\t" + reg + ", #" + std::to_string(part) +
                    ", lsl #" + std::to_string(16 * i));
    }
}

void Arm64DarwinEmitter::loadIntConstant(int32_t value) {
    materialise("w0", static_cast<uint32_t>(value), false);
}

// Built in a scratch integer register and moved across. x9 is a caller-saved
// temporary, so nothing of ours is live in it.
void Arm64DarwinEmitter::loadRealConstant(double value) {
    materialise("x9", bitsOf(value), true);
    instruction("fmov\td0, x9");
}

void Arm64DarwinEmitter::storeSlot(Slot kind, int slot) {
    instruction(std::string("str\t") + registerFile(kind) + "0, " + slotAddress(slot));
}

void Arm64DarwinEmitter::loadSlot(Slot kind, int slot) {
    instruction(std::string("ldr\t") + registerFile(kind) + "0, " + slotAddress(slot));
}

void Arm64DarwinEmitter::setArg(Slot kind, int index) {
    if (index == 0) return;           // the accumulator is already register 0
    const char file = registerFile(kind);
    const std::string move = kind == Slot::Real ? "fmov\t" : "mov\t";
    instruction(move + file + std::to_string(index) + ", " + file + "0");
}

void Arm64DarwinEmitter::loadSlotIntoArg(Slot kind, int slot, int index) {
    instruction(std::string("ldr\t") + registerFile(kind) + std::to_string(index) +
                ", " + slotAddress(slot));
}

void Arm64DarwinEmitter::spillArgument(Slot kind, int registerIndex, int slot) {
    instruction(std::string("str\t") + registerFile(kind) + std::to_string(registerIndex) +
                ", " + slotAddress(slot));
}

void Arm64DarwinEmitter::call(const std::string &name) {
    instruction("bl\t" + symbol(name));
}

void Arm64DarwinEmitter::loadSlotAddress(int slot) {
    instruction("add\tx0, sp, #" + std::to_string(8 * slot));
}

// x9 is a caller-saved temporary, so the pointer can be brought out of its
// slot without disturbing the accumulator that is about to be written.
void Arm64DarwinEmitter::storeThroughPointer(Slot kind, int pointerSlot) {
    instruction("ldr\tx9, " + slotAddress(pointerSlot));
    instruction(std::string("str\t") + registerFile(kind) + "0, [x9]");
}

void Arm64DarwinEmitter::loadThroughPointer(Slot kind, int pointerSlot) {
    instruction("ldr\tx9, " + slotAddress(pointerSlot));
    instruction(std::string("ldr\t") + registerFile(kind) + "0, [x9]");
}

std::string Arm64DarwinEmitter::bytesLabel(int id) {
    return "lshmb" + std::to_string(id);
}

void Arm64DarwinEmitter::defineBytes(int id, const std::string &bytes) {
    data_ += bytesLabel(id) + ":\n";
    std::string line;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (line.empty()) line = "\t.byte\t";
        else line += ", ";
        line += std::to_string(static_cast<unsigned char>(bytes[i]));
        if (i % 16 == 15) { data_ += line + "\n"; line.clear(); }
    }
    if (!line.empty()) data_ += line + "\n";
}

// A page and an offset: Mach-O has no single instruction that reaches an
// arbitrary address, and the pair is what the linker knows how to relax.
void Arm64DarwinEmitter::loadBytesAddress(int id) {
    instruction("adrp\tx0, " + bytesLabel(id) + "@PAGE");
    instruction("add\tx0, x0, " + bytesLabel(id) + "@PAGEOFF");
}

void Arm64DarwinEmitter::widenAccumulator() {
    instruction("sxtw\tx0, w0");
}

void Arm64DarwinEmitter::label(int id) {
    raw(labelName(id) + ":");
}

void Arm64DarwinEmitter::jump(int id) {
    instruction("b\t" + labelName(id));
}

void Arm64DarwinEmitter::jumpIfZero(int id) {
    instruction("cbz\tw0, " + labelName(id));
}

}  // namespace shalimar
