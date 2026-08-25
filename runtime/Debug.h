
#pragma once

#include <cstdint>

namespace shm {

class Debug {
public:
    static Debug &shared();

    void begin();
    bool armed() const { return armed_; }

    void at(int32_t unit, int32_t line);

    void ending(int status);

private:

    static const int capacity = 100;

    enum Mode {
        Running,
        Stepping,
        Over,
        Out
    };

    bool armed_ = false;
    Mode mode_ = Running;
    int32_t restDepth_ = 0;
    int32_t count_ = 0;
    int32_t units_[capacity] = {0};
    int32_t lines_[capacity] = {0};

    bool isBreakpoint(int32_t unit, int32_t line) const;
    void addBreakpoint(int32_t unit, int32_t line);
    void removeBreakpoint(int32_t unit, int32_t line);

    void converse(int32_t unit, int32_t line);
    void say(const char *form, int32_t a, int32_t b, int32_t c);
};

int32_t callDepth();

}
