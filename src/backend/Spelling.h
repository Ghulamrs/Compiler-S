
#pragma once

#include <cstdint>
#include <string>

namespace shalimar {

enum class Reg {
    Ax, Cx, Dx, Bx, Si, Di, Bp, Sp,
    R8, R9, R10, R11, R12, R13, R14, R15,
    Xmm0, Xmm1, Xmm2, Xmm3, Xmm4, Xmm5, Xmm6, Xmm7,

    Xmm8
};

class Spelling {
public:
    virtual ~Spelling() = default;

    virtual std::string reg(Reg r, int width) const = 0;
    virtual std::string imm(int64_t value) const = 0;

    virtual std::string wideImm(uint64_t value) const = 0;

    virtual std::string binary(const char *mnemonic, int width,
                               const std::string &src, const std::string &dst) const = 0;
    virtual std::string unary(const char *mnemonic, int width,
                              const std::string &operand) const = 0;
    virtual std::string call(const std::string &target) const = 0;
    virtual std::string ret() const = 0;

    virtual std::string widen32To64(const std::string &src,
                                    const std::string &dst) const = 0;

    virtual std::string loadAddress(const std::string &from,
                                    const std::string &dst) const = 0;

    virtual std::string indirect(Reg base, int width) const = 0;
    virtual std::string offsetFrom(Reg base, int offset, int width) const = 0;

    virtual std::string frameSlot(int offset, int width) const = 0;

    virtual std::string byteArrayHead() const = 0;
    virtual std::string byteDirective() const = 0;

    virtual std::string dataReference(const std::string &label) const = 0;

protected:

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
    std::string widen32To64(const std::string &src, const std::string &dst) const override;
    std::string loadAddress(const std::string &from, const std::string &dst) const override;
    std::string indirect(Reg base, int width) const override;
    std::string offsetFrom(Reg base, int offset, int width) const override;
    std::string frameSlot(int offset, int width) const override;
    std::string byteArrayHead() const override;
    std::string byteDirective() const override;
    std::string dataReference(const std::string &label) const override;

private:

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
    std::string widen32To64(const std::string &src, const std::string &dst) const override;
    std::string loadAddress(const std::string &from, const std::string &dst) const override;
    std::string indirect(Reg base, int width) const override;
    std::string offsetFrom(Reg base, int offset, int width) const override;
    std::string frameSlot(int offset, int width) const override;
    std::string byteArrayHead() const override;
    std::string byteDirective() const override;
    std::string dataReference(const std::string &label) const override;
};

}
