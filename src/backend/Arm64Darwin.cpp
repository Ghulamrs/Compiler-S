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

void Arm64DarwinEmitter::scratchConstant(uint64_t value) {
    materialise("x16", value, true);
}

std::string Arm64DarwinEmitter::slotAddress(int slot, int width) {
    const int offset = 8 * slot;

    const int ceiling = 4095 * width;
    if (offset <= ceiling) return "[sp, #" + std::to_string(offset) + "]";
    scratchConstant(static_cast<uint64_t>(offset));
    instruction("add\tx16, sp, x16");
    return "[x16]";
}

void Arm64DarwinEmitter::adjustStack(const char *op, int bytes) {
    if (bytes <= 0) return;
    if (bytes <= 4095) {
        instruction(std::string(op) + "\tsp, sp, #" + std::to_string(bytes));
        return;
    }
    scratchConstant(static_cast<uint64_t>(bytes));
    instruction(std::string(op) + "\tsp, sp, x16");
}

int Arm64DarwinEmitter::byteWidth(Slot kind) {
    return kind == Slot::Int ? 4 : 8;
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

    frameBytes_ = (slots * 8 + 15) & ~15;

    adjustStack("add", frameBytes_);
    instruction("ldp\tx29, x30, [sp], #" + std::to_string(frameRecordBytes));
    instruction("ret");

    const size_t bodyMark = text_.size();
    adjustStack("sub", frameBytes_);
    std::string reservation = text_.substr(bodyMark);
    text_.resize(bodyMark);

    std::string prologue;
    prologue += "\tstp\tx29, x30, [sp, #-" + std::to_string(frameRecordBytes) + "]!\n";
    prologue += "\tmov\tx29, sp\n";
    prologue += reservation;
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

void Arm64DarwinEmitter::loadRealConstant(double value) {
    materialise("x9", bitsOf(value), true);
    instruction("fmov\td0, x9");
}

void Arm64DarwinEmitter::storeSlot(Slot kind, int slot) {
    instruction(std::string("str\t") + registerFile(kind) + "0, " + slotAddress(slot, byteWidth(kind)));
}

void Arm64DarwinEmitter::loadSlot(Slot kind, int slot) {
    instruction(std::string("ldr\t") + registerFile(kind) + "0, " + slotAddress(slot, byteWidth(kind)));
}

void Arm64DarwinEmitter::setArg(Slot kind, int index) {
    if (index == 0) return;
    const char file = registerFile(kind);
    const std::string move = kind == Slot::Real ? "fmov\t" : "mov\t";
    instruction(move + file + std::to_string(index) + ", " + file + "0");
}

void Arm64DarwinEmitter::loadSlotIntoArg(Slot kind, int slot, int index) {
    instruction(std::string("ldr\t") + registerFile(kind) + std::to_string(index) +
                ", " + slotAddress(slot, byteWidth(kind)));
}

void Arm64DarwinEmitter::spillArgument(Slot kind, int registerIndex, int slot) {
    instruction(std::string("str\t") + registerFile(kind) + std::to_string(registerIndex) +
                ", " + slotAddress(slot, byteWidth(kind)));
}

void Arm64DarwinEmitter::setOverflowBlock(int slot) {
    const int offset = 8 * slot;
    if (offset <= 4095) {
        instruction("add\tx9, sp, #" + std::to_string(offset));
        return;
    }
    scratchConstant(static_cast<uint64_t>(offset));
    instruction("add\tx9, sp, x16");
}

void Arm64DarwinEmitter::spillOverflowArgument(Slot kind, int index, int slot) {
    const char file = kind == Slot::Real ? 'd' : 'x';
    const std::string reg = std::string(1, file) + "17";
    instruction("ldr\t" + reg + ", [x9, #" + std::to_string(8 * index) + "]");
    instruction("str\t" + std::string(1, registerFile(kind)) + "17, " +
                slotAddress(slot, byteWidth(kind)));
}

void Arm64DarwinEmitter::call(const std::string &name) {
    instruction("bl\t" + symbol(name));
}

void Arm64DarwinEmitter::loadSlotAddress(int slot) {
    const int offset = 8 * slot;
    if (offset <= 4095) {
        instruction("add\tx0, sp, #" + std::to_string(offset));
        return;
    }
    scratchConstant(static_cast<uint64_t>(offset));
    instruction("add\tx0, sp, x16");
}

void Arm64DarwinEmitter::storeThroughPointer(Slot kind, int pointerSlot) {
    instruction("ldr\tx17, " + slotAddress(pointerSlot, 8));
    instruction(std::string("str\t") + registerFile(kind) + "0, [x17]");
}

void Arm64DarwinEmitter::loadThroughPointer(Slot kind, int pointerSlot) {
    instruction("ldr\tx17, " + slotAddress(pointerSlot, 8));
    instruction(std::string("ldr\t") + registerFile(kind) + "0, [x17]");
}

void Arm64DarwinEmitter::defineGlobals(int slots) {
    if (slots <= 0) return;
    blank();
    raw("\t.globl\t" + symbol("shm_globals"));
    raw("\t.zerofill\t__DATA,__bss," + symbol("shm_globals") + "," +
        std::to_string(slots * 8) + ",3");
}

void Arm64DarwinEmitter::addressGlobals() {
    instruction("adrp\tx17, " + symbol("shm_globals") + "@PAGE");
    instruction("add\tx17, x17, " + symbol("shm_globals") + "@PAGEOFF");
}

std::string Arm64DarwinEmitter::globalAddress(int index, int width) {
    addressGlobals();
    const int offset = 8 * index;
    if (offset <= 4095 * width) return "[x17, #" + std::to_string(offset) + "]";
    scratchConstant(static_cast<uint64_t>(offset));
    instruction("add\tx17, x17, x16");
    return "[x17]";
}

void Arm64DarwinEmitter::loadGlobal(Slot kind, int index) {
    const std::string where = globalAddress(index, byteWidth(kind));
    instruction(std::string("ldr\t") + registerFile(kind) + "0, " + where);
}

void Arm64DarwinEmitter::storeGlobal(Slot kind, int index) {
    const std::string where = globalAddress(index, byteWidth(kind));
    instruction(std::string("str\t") + registerFile(kind) + "0, " + where);
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

}
