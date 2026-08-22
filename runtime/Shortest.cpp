// The shortest decimal spelling of a double. See Shortest.h.
#include "Shortest.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace shm {

void Shortest::write(char *out, size_t size, double v) {
    if (std::isnan(v)) { std::snprintf(out, size, "nan"); return; }
    if (std::isinf(v)) { std::snprintf(out, size, v < 0 ? "-inf" : "inf"); return; }

    char digits[32];
    int exponent = 0;
    const int count = shortestDigits(v, digits, exponent);
    const bool negative = std::signbit(v);

    // `exponent` is the power of ten of the first digit plus one, so that the
    // value is 0.d1d2... x 10^exponent. Positional layout is used while the
    // point still falls inside or just after the digits, which is where the
    // app's own conversion puts the boundary.
    char body[400];
    size_t at = 0;
    if (exponent > 16 || exponent < -3) {
        body[at++] = digits[0];
        // A single digit takes no point at all in exponent form: the app
        // prints 1e+16, not 1.0e+16. Positional form is the opposite way
        // round and always keeps one, which is why the two are written out
        // separately rather than sharing a tail.
        if (count > 1) {
            body[at++] = '.';
            for (int i = 1; i < count; ++i) body[at++] = digits[i];
        }
        const int e = exponent - 1;
        at += static_cast<size_t>(std::snprintf(body + at, sizeof body - at, "e%c%02d",
                                                e < 0 ? '-' : '+', e < 0 ? -e : e));
    } else if (exponent <= 0) {
        body[at++] = '0';
        body[at++] = '.';
        for (int i = 0; i < -exponent; ++i) body[at++] = '0';
        for (int i = 0; i < count; ++i) body[at++] = digits[i];
        body[at] = '\0';
    } else if (count <= exponent) {
        for (int i = 0; i < count; ++i) body[at++] = digits[i];
        for (int i = count; i < exponent; ++i) body[at++] = '0';
        body[at++] = '.';
        body[at++] = '0';
        body[at] = '\0';
    } else {
        for (int i = 0; i < exponent; ++i) body[at++] = digits[i];
        body[at++] = '.';
        for (int i = exponent; i < count; ++i) body[at++] = digits[i];
        body[at] = '\0';
    }
    if (exponent > 16 || exponent < -3) { /* snprintf terminated it */ }
    else body[at] = '\0';

    std::snprintf(out, size, "%s%s", negative ? "-" : "", body);
}

// The fewest significant digits that read back as the same double. Asked for
// one more each time rather than computed, because correctness here is what
// matters and seventeen tries is not a cost anyone can measure.
int Shortest::shortestDigits(double v, char *digits, int &exponent) {
    char buffer[64];
    for (int precision = 1; precision <= 17; ++precision) {
        std::snprintf(buffer, sizeof buffer, "%.*e", precision - 1, v);
        if (std::strtod(buffer, nullptr) == v) return unpack(buffer, digits, exponent);
    }
    std::snprintf(buffer, sizeof buffer, "%.17e", v);
    return unpack(buffer, digits, exponent);
}

// Takes '-1.2340e+05' apart into the digits '1234' and the exponent 6,
// dropping the trailing zeros printf insisted on.
int Shortest::unpack(const char *buffer, char *digits, int &exponent) {
    int count = 0;
    const char *p = buffer;
    if (*p == '-' || *p == '+') ++p;
    for (; *p && *p != 'e' && *p != 'E'; ++p) {
        if (*p >= '0' && *p <= '9') digits[count++] = *p;
    }
    while (count > 1 && digits[count - 1] == '0') --count;
    exponent = (*p ? std::atoi(p + 1) : 0) + 1;
    return count;
}

std::string Shortest::of(double v) {
    char text[400];
    write(text, sizeof text, v);
    return text;
}

}  // namespace shm
