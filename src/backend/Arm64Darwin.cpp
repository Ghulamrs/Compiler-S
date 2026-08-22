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

void Arm64DarwinEmitter::loadInt(int32_t value) {
    // mov takes a sixteen-bit immediate, so a wider constant is built from
    // its two halves. The assembler's synthetic 'mov reg, #imm' would do this
    // too, but only for a value it can encode - writing both halves here
    // means every int32 is reachable, negative ones included.
    const uint32_t bits = static_cast<uint32_t>(value);
    instruction("movz\tw0, #" + std::to_string(bits & 0xFFFFu));
    const uint32_t high = (bits >> 16) & 0xFFFFu;
    if (high != 0) instruction("movk\tw0, #" + std::to_string(high) + ", lsl #16");
}

void Arm64DarwinEmitter::spillInt(int slot) {
    instruction("str\tw0, " + slotAddress(slot));
}

void Arm64DarwinEmitter::loadSlotIntoIntArg(int slot, int index) {
    instruction("ldr\tw" + std::to_string(index) + ", " + slotAddress(slot));
}

void Arm64DarwinEmitter::setIntArg(int index) {
    if (index == 0) return;           // the accumulator is already w0
    instruction("mov\tw" + std::to_string(index) + ", w0");
}

void Arm64DarwinEmitter::callRuntime(const std::string &name) {
    instruction("bl\t" + symbol(name));
}

}  // namespace shalimar
