#include "Emitter.h"

#include <cstring>

namespace shalimar {

uint64_t Emitter::bitsOf(double value) {
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return bits;
}

}
