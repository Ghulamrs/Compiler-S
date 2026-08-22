// Inside the runtime.
//
// Nothing here is visible to compiled code - shmrt.h is that interface. These
// are the objects the entry points are written in terms of: where the program
// is, how it fails, how a double is spelled, what the console knows about the
// line it is part way through, and what an array is.
#pragma once

#include "shmrt.h"

#include <cstddef>

namespace shm {

// Where the program is. A runtime error names the statement executing, which
// is what the compiler sets before each one.
class Position {
public:
    static Position &shared();

    void set(int32_t line) { line_ = line; }
    int32_t line() const { return line_; }

private:
    int32_t line_ = 0;
};

// A runtime failure. Output already printed survives - the error follows it
// rather than replacing it - so the console is flushed before the message is
// written and the message goes to the same stream, keeping the order.
//
// There is deliberately no stage name in the text. Which stage caught a
// problem is an implementation detail, and the vocabulary of a compiler's
// internals has no place in a diagnostic aimed at someone writing a program.
[[noreturn]] void fail(const char *message);

// How a real is written when fixed notation will not do.
//
// Past 1e15 a double has no significant digits left to land after the point,
// so fixed notation would print hundreds of fabricated ones - 1e300 comes out
// as 309 characters. Those values, and the non-finite ones, keep the compact
// spelling instead, and the compact spelling has to be the same one the app
// prints or the two implementations disagree about what a number looks like.
//
// That spelling is the shortest decimal that reads back as the same double,
// laid out positionally while the exponent allows and in exponent form after
// - 1e15 prints as 1000000000000000.0 and 1e16 as 1e+16, with no point at all
// on a single digit.
class Shortest {
public:
    // Writes into `out`, which must hold at least 40 characters.
    static void write(char *out, size_t size, double v);

private:
    static int shortestDigits(double v, char *digits, int &exponent);
    static int unpack(const char *buffer, char *digits, int &exponent);
};

// An array, and the one representation the whole language uses for text too.
//
// Above rank one it is an array of arrays rather than a block with strides,
// because the language measures dimensions rather than declaring them: after
// 'real A[2][5]' and 'A[0] : {1.,2.}', 'A[0].row' is 2. A row is a value that
// can be replaced, so a row has to be a thing.
struct Array {
    int32_t count;
    int32_t element;      // SHM_INT, SHM_REAL, SHM_CHAR or SHM_REF
    void *data;

    int32_t *ints() const { return static_cast<int32_t *>(data); }
    double *reals() const { return static_cast<double *>(data); }
    unsigned char *chars() const { return static_cast<unsigned char *>(data); }
    Array **refs() const { return static_cast<Array **>(data); }
};

// Element kinds, shared with the compiler through shmrt.h.
enum { KindInt = 0, KindReal = 1, KindChar = 2, KindRef = 3 };

// The text an array actually holds: everything before the terminator, which
// is what makes a name in a char[20] equal to the same name in a char[128].
int32_t textLength(const Array *array);

// The console. It knows one thing beyond how to write a value: whether
// anything stands on the current line, which the grid rule consults - a
// multi-row grid starts on its own line so a label before it does not push
// the first row out of column.
class Console {
public:
    static Console &shared();

    void printInt(int32_t value);
    void printReal(double value);
    void printChar(unsigned char value);
    void printArray(const Array *array);
    void endLine();
    void flush();

    // '? prec(n)' - a directive, not a value. It applies from that point on,
    // including the rest of its own line, and is reset when a program starts
    // so that one run's setting cannot leak into the next.
    void setPlaces(int32_t requested);
    void resetPlaces();

    int scalarPlaces() const { return scalarPlaces_; }
    int gridPlaces() const { return gridPlaces_; }

private:
    // A real prints to a fixed number of places rather than to the shortest
    // spelling that round trips, because a column is read down its digits and
    // '1.0' beside '0.3333333333333333' cannot be. A grid cell gets one place
    // fewer than a scalar: a matrix row has to fit the editor's line, which
    // holds about 47 characters, and at six places a four-column matrix fits
    // where at seven it does not.
    static const int defaultScalarPlaces = 7;
    static const int defaultGridPlaces = 6;

    bool lineHasText_ = false;
    int scalarPlaces_ = defaultScalarPlaces;
    int gridPlaces_ = defaultGridPlaces;

    void emit(const char *text);
};

}  // namespace shm
