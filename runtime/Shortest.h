// How a double is spelled when fixed notation will not do.
//
// Shared by the compiler and the runtime, which both need it and must agree:
// the compiler quotes a real literal back in a diagnostic, and the runtime
// prints one past the range where fixed notation means anything. Two copies
// of this would be two spellings of the same number.
//
// Past 1e15 a double has no significant digits left to land after the point,
// so fixed notation would print hundreds of fabricated ones - 1e300 comes out
// as 309 characters. Those values, and the non-finite ones, keep the compact
// spelling instead, and the compact spelling has to be the app's or the two
// implementations disagree about what a number looks like.
//
// That spelling is the shortest decimal that reads back as the same double,
// laid out positionally while the exponent allows and in exponent form after
// - 1e15 prints as 1000000000000000.0 and 1e16 as 1e+16, with no point at all
// on a single digit. It is checked against Swift's own conversion in
// tests/shortest.sh.
#pragma once

#include <cstddef>
#include <string>

namespace shm {

class Shortest {
public:
    // Writes into `out`, which must hold at least 400 characters.
    static void write(char *out, size_t size, double v);
    static std::string of(double v);

private:
    static int shortestDigits(double v, char *digits, int &exponent);
    static int unpack(const char *buffer, char *digits, int &exponent);
};

}  // namespace shm
