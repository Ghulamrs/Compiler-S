#include "X86_64.h"

#include <algorithm>

namespace shalimar {
namespace {

const Reg systemVIntArgs[] = {Reg::Di, Reg::Si, Reg::Dx, Reg::Cx, Reg::R8, Reg::R9};
const Reg microsoftIntArgs[] = {Reg::Cx, Reg::Dx, Reg::R8, Reg::R9};

}  // namespace

const Abi &systemVAbi() {
    static const Abi abi = {systemVIntArgs, 6, 0};
    return abi;
}

const Abi &microsoftAbi() {
    static const Abi abi = {microsoftIntArgs, 4, 32};
    return abi;
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

void X86_64Emitter::loadInt(int32_t value) {
    instruction(spelling_.binary("mov", 4, spelling_.imm(value), spelling_.reg(accumulator, 4)));
}

void X86_64Emitter::spillInt(int slot) {
    instruction(spelling_.binary("mov", 4, spelling_.reg(accumulator, 4), slotOperand(slot, 4)));
}

void X86_64Emitter::loadSlotIntoIntArg(int slot, int index) {
    instruction(spelling_.binary("mov", 4, slotOperand(slot, 4),
                                 spelling_.reg(abi_.intArgs[index], 4)));
}

void X86_64Emitter::setIntArg(int index) {
    const Reg target = abi_.intArgs[index];
    instruction(spelling_.binary("mov", 4, spelling_.reg(accumulator, 4), spelling_.reg(target, 4)));
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
