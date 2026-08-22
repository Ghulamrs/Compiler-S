// Printing.
//
// Each item is followed by a single space and the newline is appended once at
// the end, so '?' always leaves a trailing space before its newline.
//
// A numeric array prints as a grid, right-aligned in columns, so a program
// needs no display function of its own. A char array is not a grid - it
// prints as text, inline. A multi-row grid starts on its own line, so a label
// written before it does not push the first row out of column; that is the
// one rule that needs to know whether anything stands on the line already.
#include "Internal.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace shm {
namespace {

// A real prints to a fixed number of decimal places. Past 1e15 a double has
// no significant digits left to land after the point, so those values and the
// non-finite ones keep the compact spelling instead.
std::string fixed(double v, int places) {
    char text[400];
    if (std::isfinite(v) && v < 1e15 && v > -1e15) {
        std::snprintf(text, sizeof text, "%.*f", places, v);
    } else {
        Shortest::write(text, sizeof text, v);
    }
    return text;
}

bool isText(const Array *array) {
    return array && array->element == KindChar;
}

std::string cell(const Array *array, int32_t i, int places) {
    switch (array->element) {
    case KindInt:  return std::to_string(array->ints()[i]);
    case KindReal: return fixed(array->reals()[i], places);
    case KindChar: return array->chars()[i] == 0
                              ? std::string()
                              : std::string(1, static_cast<char>(array->chars()[i]));
    default:       return std::string();
    }
}

// Flattens to rows: an array of scalars is one row, an array of arrays is
// whatever its elements flatten to, in order.
void collect(const Array *array, int places, std::vector<std::vector<std::string>> &rows) {
    if (array->element != KindRef) {
        std::vector<std::string> row;
        for (int32_t i = 0; i < array->count; ++i) row.push_back(cell(array, i, places));
        rows.push_back(row);
        return;
    }
    for (int32_t i = 0; i < array->count; ++i) collect(array->refs()[i], places, rows);
}

}  // namespace

Console &Console::shared() {
    static Console instance;
    return instance;
}

void Console::emit(const char *text) {
    std::printf("%s", text);
    lineHasText_ = true;
}

void Console::printInt(int32_t value) {
    std::printf("%d ", static_cast<int>(value));
    lineHasText_ = true;
}

void Console::printReal(double value) {
    emit((fixed(value, scalarPlaces_) + " ").c_str());
}

// A char is a character, not a number, and char(0) is the terminator - which
// prints as nothing at all.
void Console::printChar(unsigned char value) {
    if (value == 0) emit(" ");
    else { const char text[3] = {static_cast<char>(value), ' ', '\0'}; emit(text); }
}

void Console::printArray(const Array *array) {
    if (!array) { emit(" "); return; }

    if (isText(array)) {
        const int32_t n = textLength(array);
        std::string out(reinterpret_cast<const char *>(array->chars()),
                        static_cast<size_t>(n));
        emit((out + " ").c_str());
        return;
    }

    std::vector<std::vector<std::string>> rows;
    collect(array, gridPlaces_, rows);

    size_t width = 0;
    for (const std::vector<std::string> &row : rows) {
        for (const std::string &text : row) {
            if (text.size() > width) width = text.size();
        }
    }

    std::string out;
    for (size_t r = 0; r < rows.size(); ++r) {
        if (r > 0) out += "\n";
        for (size_t c = 0; c < rows[r].size(); ++c) {
            if (c > 0) out += "  ";
            out.append(width - rows[r][c].size(), ' ');
            out += rows[r][c];
        }
    }
    if (rows.size() > 1 && lineHasText_) std::printf("\n");
    emit((out + " ").c_str());
}

void Console::endLine() {
    std::putchar('\n');
    lineHasText_ = false;
}

void Console::flush() { std::fflush(stdout); }

// prec(n) runs -1 through 24 and clamps to that range at both ends, so
// nothing is ever refused. 24 is past the seventeen significant digits a
// double carries, while still leaving room to read a 1e-20 tolerance.
void Console::setPlaces(int32_t requested) {
    int places = requested < -1 ? -1 : (requested > 24 ? 24 : requested);
    if (places < 0) { resetPlaces(); return; }
    scalarPlaces_ = places;
    gridPlaces_ = places;
}

void Console::resetPlaces() {
    scalarPlaces_ = defaultScalarPlaces;
    gridPlaces_ = defaultGridPlaces;
}

}  // namespace shm

extern "C" {

void shm_print_int(int32_t value) { shm::Console::shared().printInt(value); }
void shm_print_real(double value) { shm::Console::shared().printReal(value); }

void shm_print_char(int32_t value) {
    shm::Console::shared().printChar(static_cast<unsigned char>(value));
}

void shm_print_array(const ShmArray *array) {
    shm::Console::shared().printArray(reinterpret_cast<const shm::Array *>(array));
}

void shm_print_places(int32_t places) { shm::Console::shared().setPlaces(places); }

void shm_line_end(void) { shm::Console::shared().endLine(); }

}  // extern "C"
