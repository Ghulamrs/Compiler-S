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

Reg X86_64Emitter::registerFor(Slot kind) {
    return kind == Slot::Real ? realAccumulator : accumulator;
}

// 'movsd' moves one double and carries its own width, so it takes no suffix;
// 'mov' takes one from the operand width.
const char *X86_64Emitter::moveFor(Slot kind) {
    return kind == Slot::Real ? "movsd" : "mov";
}

int X86_64Emitter::widthFor(Slot kind) {
    return kind == Slot::Int ? 4 : 8;
}

Reg X86_64Emitter::sseArg(int index) {
    static const Reg sse[] = {Reg::Xmm0, Reg::Xmm1, Reg::Xmm2, Reg::Xmm3,
                              Reg::Xmm4, Reg::Xmm5, Reg::Xmm6, Reg::Xmm7};
    return sse[index];
}

Reg X86_64Emitter::argRegister(Slot kind, int index) const {
    return kind == Slot::Real ? sseArg(index) : abi_.intArgs[index];
}

// On entry rsp is eight past a multiple of sixteen; pushing rbp squares it,
// so the reservation itself has to be a multiple of sixteen to leave it that
// way for the next call.
int X86_64Emitter::frameBytesFor(int slots) const {
    return (slots * 8 + abi_.shadowBytes + 15) & ~15;
}

std::string X86_64Emitter::slotOperand(int slot, int width) const {
    return spelling_.frameSlot(abi_.shadowBytes + 8 * slot, width);
}

std::string X86_64Emitter::prologue(int slots) {
    const int bytes = frameBytesFor(slots);
    std::string out;
    out += "\t" + spelling_.unary("push", 8, spelling_.reg(Reg::Bp, 8)) + "\n";
    out += "\t" + spelling_.binary("mov", 8, spelling_.reg(Reg::Sp, 8),
                                    spelling_.reg(Reg::Bp, 8)) + "\n";
    if (bytes > 0) {
        out += "\t" + spelling_.binary("sub", 8, spelling_.imm(bytes),
                                        spelling_.reg(Reg::Sp, 8)) + "\n";
    }
    return out;
}

void X86_64Emitter::emitEpilogue(int slots) {
    const int bytes = frameBytesFor(slots);
    if (bytes > 0) {
        instruction(spelling_.binary("add", 8, spelling_.imm(bytes),
                                     spelling_.reg(Reg::Sp, 8)));
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
                                 spelling_.reg(accumulator, 8)));
    // No width suffix: 'movq' between a general register and an SSE one is
    // its own mnemonic, and GNU would otherwise make it 'movqq'.
    instruction(spelling_.binary("movq", 0, spelling_.reg(accumulator, 8),
                                 spelling_.reg(realAccumulator, 8)));
}

void X86_64Emitter::storeSlot(Slot kind, int slot) {
    const int width = widthFor(kind);
    instruction(spelling_.binary(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 spelling_.reg(registerFor(kind), width),
                                 slotOperand(slot, width)));
}

void X86_64Emitter::loadSlot(Slot kind, int slot) {
    const int width = widthFor(kind);
    instruction(spelling_.binary(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 slotOperand(slot, width),
                                 spelling_.reg(registerFor(kind), width)));
}

void X86_64Emitter::setArg(Slot kind, int index) {
    const Reg target = argRegister(kind, index);
    if (target == registerFor(kind)) return;
    const int width = widthFor(kind);
    // Between two SSE registers it is 'movapd': 'movsd' would leave the upper
    // half of the destination as it found it, which is a partial-register
    // dependency and not what a move should mean.
    const char *mnemonic = kind == Slot::Real ? "movapd" : "mov";
    instruction(spelling_.binary(mnemonic, kind == Slot::Real ? 0 : width,
                                 spelling_.reg(registerFor(kind), width),
                                 spelling_.reg(target, width)));
}

void X86_64Emitter::loadSlotIntoArg(Slot kind, int slot, int index) {
    const int width = widthFor(kind);
    instruction(spelling_.binary(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 slotOperand(slot, width),
                                 spelling_.reg(argRegister(kind, index), width)));
}

void X86_64Emitter::spillArgument(Slot kind, int registerIndex, int slot) {
    const int width = widthFor(kind);
    instruction(spelling_.binary(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 spelling_.reg(argRegister(kind, registerIndex), width),
                                 slotOperand(slot, width)));
}

void X86_64Emitter::loadSlotAddress(int slot) {
    instruction(spelling_.loadAddress(slotOperand(slot, 8), spelling_.reg(accumulator, 8)));
}

// r10 is caller-saved in both conventions and is not an argument register in
// either, so the pointer can be brought out of its slot without disturbing
// anything the call is about to want.
void X86_64Emitter::storeThroughPointer(Slot kind, int pointerSlot) {
    instruction(spelling_.binary("mov", 8, slotOperand(pointerSlot, 8),
                                 spelling_.reg(Reg::R10, 8)));
    const int width = widthFor(kind);
    instruction(spelling_.binary(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 spelling_.reg(registerFor(kind), width),
                                 spelling_.indirect(Reg::R10, width)));
}

void X86_64Emitter::loadThroughPointer(Slot kind, int pointerSlot) {
    instruction(spelling_.binary("mov", 8, slotOperand(pointerSlot, 8),
                                 spelling_.reg(Reg::R10, 8)));
    const int width = widthFor(kind);
    instruction(spelling_.binary(moveFor(kind), kind == Slot::Real ? 0 : width,
                                 spelling_.indirect(Reg::R10, width),
                                 spelling_.reg(registerFor(kind), width)));
}

void X86_64Emitter::defineBytes(int id, const std::string &bytes) {
    std::string line = bytesLabel(id) + spelling_.byteArrayHead();
    bool opened = false;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (opened) line += ", ";
        line += std::to_string(static_cast<unsigned char>(bytes[i]));
        opened = true;
        if (i % 16 == 15 && i + 1 < bytes.size()) {
            data_ += line + "\n";
            line = spelling_.byteDirective();
            opened = false;
        }
    }
    data_ += line + "\n";
}

void X86_64Emitter::loadBytesAddress(int id) {
    instruction(spelling_.loadAddress(spelling_.dataReference(bytesLabel(id)),
                                      spelling_.reg(accumulator, 8)));
}

void X86_64Emitter::call(const std::string &name) {
    const std::string linkName = symbol(name);
    noteExternal(linkName);
    instruction(spelling_.call(linkName));
}

void X86_64Emitter::widenAccumulator() {
    instruction(spelling_.widen32To64(spelling_.reg(accumulator, 4),
                                      spelling_.reg(accumulator, 8)));
}

void X86_64Emitter::jump(int id) {
    instruction("jmp\t" + labelName(id));
}

void X86_64Emitter::jumpIfZero(int id) {
    instruction(spelling_.binary("test", 4, spelling_.reg(accumulator, 4),
                                 spelling_.reg(accumulator, 4)));
    instruction("je\t" + labelName(id));
}

void X86_64Emitter::noteExternal(const std::string &name) {
    if (std::find(referenced_.begin(), referenced_.end(), name) == referenced_.end()) {
        referenced_.push_back(name);
    }
}

void X86_64Emitter::noteDefined(const std::string &name) {
    if (std::find(defined_.begin(), defined_.end(), name) == defined_.end()) {
        defined_.push_back(name);
    }
}

std::vector<std::string> X86_64Emitter::externals() const {
    std::vector<std::string> out;
    for (const std::string &name : referenced_) {
        if (std::find(defined_.begin(), defined_.end(), name) == defined_.end()) {
            out.push_back(name);
        }
    }
    return out;
}

}  // namespace shalimar
