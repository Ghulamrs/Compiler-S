#include "Spelling.h"

namespace shalimar {
namespace {

struct RegNames {
    const char *wide;
    const char *dword;
    const char *byte;
};

const RegNames table[] = {
    {"rax", "eax",  "al"},    {"rcx", "ecx",  "cl"},
    {"rdx", "edx",  "dl"},    {"rbx", "ebx",  "bl"},
    {"rsi", "esi",  "sil"},   {"rdi", "edi",  "dil"},
    {"rbp", "ebp",  "bpl"},   {"rsp", "esp",  "spl"},
    {"r8",  "r8d",  "r8b"},   {"r9",  "r9d",  "r9b"},
    {"r10", "r10d", "r10b"},  {"r11", "r11d", "r11b"},
    {"r12", "r12d", "r12b"},  {"r13", "r13d", "r13b"},
    {"r14", "r14d", "r14b"},  {"r15", "r15d", "r15b"},
    {"xmm0", "xmm0", "xmm0"}, {"xmm1", "xmm1", "xmm1"},
    {"xmm2", "xmm2", "xmm2"}, {"xmm3", "xmm3", "xmm3"},
    {"xmm4", "xmm4", "xmm4"}, {"xmm5", "xmm5", "xmm5"},
    {"xmm6", "xmm6", "xmm6"}, {"xmm7", "xmm7", "xmm7"},
    {"xmm8", "xmm8", "xmm8"}
};

std::string hex(uint64_t value) {
    static const char *digits = "0123456789ABCDEF";
    std::string out;
    do {
        out.insert(out.begin(), digits[value & 0xFu]);
        value >>= 4;
    } while (value != 0);
    return out;
}

}

const char *Spelling::name(Reg r, int width) {
    const RegNames &n = table[static_cast<int>(r)];
    if (width == 8) return n.wide;
    if (width == 1) return n.byte;
    return n.dword;
}

char GnuSpelling::suffix(int width) {
    if (width == 8) return 'q';
    if (width == 1) return 'b';
    return 'l';
}

std::string GnuSpelling::reg(Reg r, int width) const {
    return std::string("%") + name(r, width);
}

std::string GnuSpelling::imm(int64_t value) const {
    return "$" + std::to_string(value);
}

std::string GnuSpelling::wideImm(uint64_t value) const {
    return "$0x" + hex(value);
}

std::string GnuSpelling::binary(const char *mnemonic, int width,
                                const std::string &src, const std::string &dst) const {
    std::string out = mnemonic;
    if (width > 0) out += suffix(width);
    return out + "\t" + src + ", " + dst;
}

std::string GnuSpelling::unary(const char *mnemonic, int width,
                               const std::string &operand) const {
    std::string out = mnemonic;
    if (width > 0) out += suffix(width);
    return out + "\t" + operand;
}

std::string GnuSpelling::call(const std::string &target) const {
    return "call\t" + target;
}

std::string GnuSpelling::ret() const { return "ret"; }

std::string GnuSpelling::frameSlot(int offset, int) const {
    return std::to_string(offset) + "(%rsp)";
}

std::string GnuSpelling::widen32To64(const std::string &src, const std::string &dst) const {
    return "movslq\t" + src + ", " + dst;
}

std::string GnuSpelling::loadAddress(const std::string &from, const std::string &dst) const {
    return "leaq\t" + from + ", " + dst;
}

std::string GnuSpelling::indirect(Reg base, int) const {
    return "(" + reg(base, 8) + ")";
}

std::string GnuSpelling::offsetFrom(Reg base, int offset, int) const {
    return std::to_string(offset) + "(" + reg(base, 8) + ")";
}

std::string GnuSpelling::byteArrayHead() const { return ":\n\t.byte\t"; }
std::string GnuSpelling::byteDirective() const { return "\t.byte\t"; }

std::string GnuSpelling::dataReference(const std::string &label) const {
    return label + "(%rip)";
}

std::string MasmSpelling::reg(Reg r, int width) const {
    return name(r, width);
}

std::string MasmSpelling::imm(int64_t value) const {
    return std::to_string(value);
}

std::string MasmSpelling::wideImm(uint64_t value) const {
    return "0" + hex(value) + "h";
}

std::string MasmSpelling::binary(const char *mnemonic, int,
                                 const std::string &src, const std::string &dst) const {
    return std::string(mnemonic) + "\t" + dst + ", " + src;
}

std::string MasmSpelling::unary(const char *mnemonic, int,
                                const std::string &operand) const {
    return std::string(mnemonic) + "\t" + operand;
}

std::string MasmSpelling::call(const std::string &target) const {
    return "call\t" + target;
}

std::string MasmSpelling::ret() const { return "ret"; }

std::string MasmSpelling::widen32To64(const std::string &src, const std::string &dst) const {
    return "movsxd\t" + dst + ", " + src;
}

std::string MasmSpelling::loadAddress(const std::string &from, const std::string &dst) const {
    return "lea\t" + dst + ", " + from;
}

std::string MasmSpelling::indirect(Reg base, int width) const {
    const char *size = width == 8 ? "QWORD" : (width == 1 ? "BYTE" : "DWORD");
    return std::string(size) + " PTR [" + reg(base, 8) + "]";
}

std::string MasmSpelling::offsetFrom(Reg base, int offset, int width) const {
    const char *size = width == 8 ? "QWORD" : (width == 1 ? "BYTE" : "DWORD");
    return std::string(size) + " PTR [" + reg(base, 8) + "+" + std::to_string(offset) + "]";
}

std::string MasmSpelling::byteArrayHead() const { return "\tDB\t"; }
std::string MasmSpelling::byteDirective() const { return "\tDB\t"; }

std::string MasmSpelling::dataReference(const std::string &label) const {
    return label;
}

std::string MasmSpelling::frameSlot(int offset, int width) const {
    const char *size = width == 8 ? "QWORD" : (width == 1 ? "BYTE" : "DWORD");
    return std::string(size) + " PTR [rsp+" + std::to_string(offset) + "]";
}

}
