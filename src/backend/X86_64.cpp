#include "X86_64.h"

#include <algorithm>

namespace shalimar {
namespace {

const Reg systemVIntArgs[] = {Reg::Di, Reg::Si, Reg::Dx, Reg::Cx, Reg::R8, Reg::R9};
const Reg microsoftIntArgs[] = {Reg::Cx, Reg::Dx, Reg::R8, Reg::R9};

}  // namespace

const Abi &systemVAbi() {
    static const Abi abi = {systemVIntArgs, 6, false, 0};
    return abi;
}

const Abi &microsoftAbi() {
    static const Abi abi = {microsoftIntArgs, 4, true, 32};
    return abi;
}

Reg X86_64Emitter::sseArg(int index) {
    static const Reg sse[] = {Reg::Xmm0, Reg::Xmm1, Reg::Xmm2, Reg::Xmm3,
                              Reg::Xmm4, Reg::Xmm5, Reg::Xmm6, Reg::Xmm7};
    return sse[index];
}

void X86_64Emitter::setSlots(int slots) {
    // On entry rsp is eight past a multiple of sixteen; pushing rbp squares
    // it, so the reservation itself has to be a multiple of sixteen to leave
    // it that way for the next call.
    frameBytes_ = (slots * 8 + abi_.shadowBytes + 15) & ~15;
}

// Slot n sits below rbp, above the shadow area at the bottom of the frame.
std::string X86_64Emitter::slotOperand(int slot, int width) const {
    return spelling_.frameSlot(-8 * (slot + 1), width);
}

void X86_64Emitter::emitPrologue() {
    instruction(spelling_.unary("push", 8, spelling_.reg(Reg::Bp, 8)));
    instruction(spelling_.binary("mov", 8, spelling_.reg(Reg::Sp, 8), spelling_.reg(Reg::Bp, 8)));
    const int bytes = frameBytes();
    if (bytes > 0) {
        instruction(spelling_.binary("sub", 8, spelling_.imm(bytes), spelling_.reg(Reg::Sp, 8)));
    }
}

void X86_64Emitter::emitEpilogue() {
    const int bytes = frameBytes();
    if (bytes > 0) {
        instruction(spelling_.binary("add", 8, spelling_.imm(bytes), spelling_.reg(Reg::Sp, 8)));
    }
    instruction(spelling_.unary("pop", 8, spelling_.reg(Reg::Bp, 8)));
    instruction(spelling_.ret());
}

void X86_64Emitter::loadIntConstant(int32_t value) {
    instruction(spelling_.binary("mov", 4, spelling_.imm(value), spelling_.reg(accumulator, 4)));
}

// Built in an integer register and moved across, which costs one extra
// instruction and saves a constant pool, a section and a relocation.
void X86_64Emitter::loadRealConstant(double value) {
    instruction(spelling_.binary("mov", 8, spelling_.wideImm(bitsOf(value)),
                                 spelling_.reg(scratch, 8)));
    // No width suffix: 'movq' between a general register and an SSE one is
    // its own mnemonic, and GNU would otherwise make it 'movqq'.
    instruction(spelling_.binary("movq", 0, spelling_.reg(scratch, 8),
                                 spelling_.reg(realAccumulator, 8)));
}

void X86_64Emitter::storeIntSlot(int slot) {
    instruction(spelling_.binary("mov", 4, spelling_.reg(accumulator, 4), slotOperand(slot, 4)));
}

void X86_64Emitter::loadIntSlot(int slot) {
    instruction(spelling_.binary("mov", 4, slotOperand(slot, 4), spelling_.reg(accumulator, 4)));
}

void X86_64Emitter::storeRealSlot(int slot) {
    instruction(spelling_.binary("movsd", 0, spelling_.reg(realAccumulator, 8),
                                 slotOperand(slot, 8)));
}

void X86_64Emitter::loadRealSlot(int slot) {
    instruction(spelling_.binary("movsd", 0, slotOperand(slot, 8),
                                 spelling_.reg(realAccumulator, 8)));
}

void X86_64Emitter::loadIntSlotIntoArg(int slot, int index) {
    instruction(spelling_.binary("mov", 4, slotOperand(slot, 4),
                                 spelling_.reg(abi_.intArgs[index], 4)));
}

void X86_64Emitter::loadRealSlotIntoArg(int slot, int index) {
    instruction(spelling_.binary("movsd", 0, slotOperand(slot, 8),
                                 spelling_.reg(sseArg(index), 8)));
}

void X86_64Emitter::setIntArg(int index) {
    const Reg target = abi_.intArgs[index];
    if (target == accumulator) return;
    instruction(spelling_.binary("mov", 4, spelling_.reg(accumulator, 4), spelling_.reg(target, 4)));
}

void X86_64Emitter::setRealArg(int index) {
    const Reg target = sseArg(index);
    if (target == realAccumulator) return;
    instruction(spelling_.binary("movapd", 0, spelling_.reg(realAccumulator, 8),
                                 spelling_.reg(target, 8)));
}

void X86_64Emitter::callRuntime(const std::string &name) {
    const std::string linkName = symbol(name);
    noteExternal(linkName);
    instruction(spelling_.call(linkName));
}

void X86_64Emitter::noteExternal(const std::string &name) {
    if (std::find(externals_.begin(), externals_.end(), name) == externals_.end()) {
        externals_.push_back(name);
    }
}

}  // namespace shalimar
