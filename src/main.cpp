#include "Driver.h"

#include <string>
#include <vector>

int main(int argc, char **argv) {
    std::vector<std::string> arguments;
    arguments.reserve(static_cast<size_t>(argc));
    for (int i = 0; i < argc; ++i) arguments.push_back(argv[i]);
    return shalimar::Driver().run(arguments);
}
