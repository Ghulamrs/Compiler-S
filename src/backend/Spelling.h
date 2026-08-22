// How an x86-64 instruction is written down.
//
// One instruction stream serves both x86-64 targets. GnuSpelling and
// MasmSpelling are the whole of what differs between them: operand order,
// the sigils, and whether the width rides on the mnemonic or on the operand.
// Anything that is a real difference between Linux and Windows - which
// registers carry arguments, how much room the caller leaves - is the Abi's
// business, not this one's, and anything that is a difference of assembler
// directives belongs to the target class. Keeping the three apart is what
// stops "Windows" becoming a synonym for "Intel syntax".
#pragma once

#include <cstdint>
#include <string>

namespace shalimar {

enum class Reg {
    Ax, Cx, Dx, Bx, Si, Di, Bp, Sp,
    R8, R9, R10, R11, R12, R13, R14, R15,
    Xmm0, Xmm1, Xmm2, Xmm3, Xmm4, Xmm5, Xmm6, Xmm7
};

class Spelling {
public:
    virtual ~Spelling() = default;

    // width is in bytes: 1, 4 or 8. An SSE register ignores it.
    virtual std::string reg(Reg r, int width) const = 0;
    virtual std::string imm(int64_t value) const = 0;

    // A full-width constant, written in hexadecimal because that is the only
    // spelling both assemblers read the same way for a value above the signed
    // range - which the bit pattern of a double regularly is.
    virtual std::string wideImm(uint64_t value) const = 0;

    // src is read and dst is written, whichever order the syntax prints them.
    virtual std::string binary(const char *mnemonic, int width,
                               const std::string &src, const std::string &dst) const = 0;
    virtual std::string unary(const char *mnemonic, int width,
                              const std::string &operand) const = 0;
    virtual std::string call(const std::string &target) const = 0;
    virtual std::string ret() const = 0;

    // Sign-extending a 32-bit register into its 64-bit self. The two
    // assemblers do not even agree on the mnemonic for it.
    virtual std::string widen32To64(const std::string &src, const std::string &dst) const = 0;

    // A frame slot: an offset from the base pointer. MASM wants the width
    // written on the reference because the register operand may not carry
    // it; GNU puts the width on the mnemonic instead and needs none here.
    virtual std::string frameSlot(int offset, int width) const = 0;

protected:
    // The register file, indexed by Reg, at each of the three widths.
    static const char *name(Reg r, int width);
};

class GnuSpelling : public Spelling {
public:
    std::string reg(Reg r, int width) const override;
    std::string imm(int64_t value) const override;
    std::string wideImm(uint64_t value) const override;
    std::string binary(const char *mnemonic, int width,
                       const std::string &src, const std::string &dst) const override;
    std::string unary(const char *mnemonic, int width,
                      const std::string &operand) const override;
    std::string call(const std::string &target) const override;
    std::string ret() const override;
    std::string frameSlot(int offset, int width) const override;
    std::string widen32To64(const std::string &src, const std::string &dst) const override;

private:
    // 'l' on a four-byte mov, 'q' on an eight-byte one. An SSE mnemonic
    // carries its own width and takes none.
    static char suffix(int width);
};

class MasmSpelling : public Spelling {
public:
    std::string reg(Reg r, int width) const override;
    std::string imm(int64_t value) const override;
    std::string wideImm(uint64_t value) const override;
    std::string binary(const char *mnemonic, int width,
                       const std::string &src, const std::string &dst) const override;
    std::string unary(const char *mnemonic, int width,
                      const std::string &operand) const override;
    std::string call(const std::string &target) const override;
    std::string ret() const override;
    std::string frameSlot(int offset, int width) const override;
    std::string widen32To64(const std::string &src, const std::string &dst) const override;
};

}  // namespace shalimar
