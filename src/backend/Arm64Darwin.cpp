#include "Arm64Darwin.h"

namespace shalimar {

void Arm64DarwinEmitter::beginModule(const std::string &sourceName) {
    raw("// " + sourceName);
    raw("\t.section\t__TEXT,__text,regular,pure_instructions");
}

void Arm64DarwinEmitter::endModule() {
    blank();
}

void Arm64DarwinEmitter::beginFunction(const std::string &name) {
    blank();
    raw("\t.globl\t" + symbol(name));
    raw("\t.p2align\t2");
    raw(symbol(name) + ":");
    // stp with a pre-index writes the pair and moves sp in one instruction,
    // which is the sixteen bytes the ABI's alignment wants anyway.
    instruction("stp\tx29, x30, [sp, #-" + std::to_string(frameRecordBytes) + "]!");
    instruction("mov\tx29, sp");
}

void Arm64DarwinEmitter::endFunction() {
    instruction("ldp\tx29, x30, [sp], #" + std::to_string(frameRecordBytes));
    instruction("ret");
}

void Arm64DarwinEmitter::loadInt(int32_t value) {
    // mov takes a 16-bit immediate. Anything wider is built with movz/movk,
    // which is what the assembler's synthetic 'mov reg, #imm' does for us -
    // but only for a value it can encode, so the two halves are written here
    // rather than trusted to it.
    const uint32_t bits = static_cast<uint32_t>(value);
    const uint32_t low = bits & 0xFFFFu;
    const uint32_t high = (bits >> 16) & 0xFFFFu;
    instruction("movz\tw0, #" + std::to_string(low));
    if (high != 0) instruction("movk\tw0, #" + std::to_string(high) + ", lsl #16");
}

void Arm64DarwinEmitter::setIntArg(int index) {
    if (index == 0) return;           // the accumulator is already w0
    instruction("mov\tw" + std::to_string(index) + ", w0");
}

void Arm64DarwinEmitter::callRuntime(const std::string &name) {
    instruction("bl\t" + symbol(name));
}

}  // namespace shalimar
