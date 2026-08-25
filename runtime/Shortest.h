
#pragma once

#include <cstddef>
#include <string>

namespace shm {

class Shortest {
public:

    static void write(char *out, size_t size, double v);
    static std::string of(double v);

private:
    static int shortestDigits(double v, char *digits, int &exponent);
    static int unpack(const char *buffer, char *digits, int &exponent);
};

}
