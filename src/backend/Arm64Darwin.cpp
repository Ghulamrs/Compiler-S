#include "Arm64Darwin.h"

namespace shalimar {

void Arm64DarwinEmitter::beginModule(const std::string &sourceName) {
    raw("// " + sourceName);
    raw("\t.section\t__TEXT,__text,regular,pure_instructions");
}

void Arm64DarwinEmitter::endModule() {
    blank();
}

std::string Arm64DarwinEmitter::slotAddress(int slot) const {
    return "[x29, #-" + std::to_string(8 * (slot + 1)) + "]";
}

void Arm64DarwinEmitter::beginFunction(const std::string &name, int slots) {
    // The stack pointer must stay sixteen-byte aligned, so the reservation is
    // rounded up rather than taken as it comes.
    frameBytes_ = (slots * 8 + 15) & ~15;

    blank();
    raw("\t.globl\t" + symbol(name));
    raw("\t.p2align\t2");
    raw(symbol(name) + ":");
    // stp with a pre-index writes the pair and moves sp in one instruction,
    // and sixteen bytes is what the alignment wanted anyway.
    instruction("stp\tx29, x30, [sp, #-" + std::to_string(frameRecordBytes) + "]!");
    instruction("mov\tx29, sp");
    if (frameBytes_ > 0) instruction("sub\tsp, sp, #" + std::to_string(frameBytes_));
}

void Arm64DarwinEmitter::endFunction() {
    if (frameBytes_ > 0) instruction("add\tsp, sp, #" + std::to_string(frameBytes_));
    instruction("ldp\tx29, x30, [sp], #" + std::to_string(frameRecordBytes));
    instruction("ret");
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

void Arm64DarwinEmitter::storeIntSlot(int slot) {
    instruction("str\tw0, " + slotAddress(slot));
}

void Arm64DarwinEmitter::loadIntSlot(int slot) {
    instruction("ldr\tw0, " + slotAddress(slot));
}

void Arm64DarwinEmitter::storeRealSlot(int slot) {
    instruction("str\td0, " + slotAddress(slot));
}

void Arm64DarwinEmitter::loadRealSlot(int slot) {
    instruction("ldr\td0, " + slotAddress(slot));
}

void Arm64DarwinEmitter::setIntArg(int index) {
    if (index == 0) return;           // the accumulator is already w0
    instruction("mov\tw" + std::to_string(index) + ", w0");
}

void Arm64DarwinEmitter::setRealArg(int index) {
    if (index == 0) return;           // the accumulator is already d0
    instruction("fmov\td" + std::to_string(index) + ", d0");
}

void Arm64DarwinEmitter::loadIntSlotIntoArg(int slot, int index) {
    instruction("ldr\tw" + std::to_string(index) + ", " + slotAddress(slot));
}

void Arm64DarwinEmitter::loadRealSlotIntoArg(int slot, int index) {
    instruction("ldr\td" + std::to_string(index) + ", " + slotAddress(slot));
}

void Arm64DarwinEmitter::callRuntime(const std::string &name) {
    instruction("bl\t" + symbol(name));
}

}  // namespace shalimar
