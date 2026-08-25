
#pragma once

#include "Shortest.h"
#include "shmrt.h"

#include <cstddef>

namespace shm {

class Position {
public:
    static Position &shared();

    void set(int32_t unit, int32_t line) { unit_ = unit; line_ = line; }
    int32_t line() const { return line_; }

    void name(int32_t unit, const char *file);

    const char *file() const;
    const char *nameOf(int32_t unit) const;
    int32_t named() const { return named_; }

private:

    static const int capacity = 64;

    int32_t unit_ = 0;
    int32_t line_ = 0;
    int32_t named_ = 0;
    const char *names_[capacity] = {0};
};

[[noreturn]] void fail(const char *message);

struct Array {
    int32_t count;
    int32_t element;
    void *data;

    int32_t *ints() const { return static_cast<int32_t *>(data); }
    double *reals() const { return static_cast<double *>(data); }
    unsigned char *chars() const { return static_cast<unsigned char *>(data); }
    Array **refs() const { return static_cast<Array **>(data); }
};

enum { KindInt = 0, KindReal = 1, KindChar = 2, KindRef = 3 };

int32_t textLength(const Array *array);

class Console {
public:
    static Console &shared();

    void printInt(int32_t value);
    void printReal(double value);
    void printChar(unsigned char value);
    void printArray(const Array *array);
    void endLine();
    void flush();

    void setPlaces(int32_t requested);
    void resetPlaces();

    int scalarPlaces() const { return scalarPlaces_; }
    int gridPlaces() const { return gridPlaces_; }

private:

    static const int defaultScalarPlaces = 7;
    static const int defaultGridPlaces = 6;

    bool lineHasText_ = false;
    int scalarPlaces_ = defaultScalarPlaces;
    int gridPlaces_ = defaultGridPlaces;

    void emit(const char *text);
};

}
