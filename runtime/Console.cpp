
#include "Internal.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace shm {
namespace {

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

void collect(const Array *array, int places, std::vector<std::vector<std::string>> &rows) {
    if (array->element != KindRef) {
        std::vector<std::string> row;
        for (int32_t i = 0; i < array->count; ++i) row.push_back(cell(array, i, places));
        rows.push_back(row);
        return;
    }
    for (int32_t i = 0; i < array->count; ++i) collect(array->refs()[i], places, rows);
}

}

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

}

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

}
