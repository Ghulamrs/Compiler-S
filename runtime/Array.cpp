// Arrays, and the text that is made of them.
//
// An array above rank one is an array of arrays rather than a block with
// strides. That is not a representation preference: the language measures
// dimensions rather than declaring them, so after 'real A[2][5]' and
// 'A[0] : {1.,2.}' the answer to 'A[0].row' is 2. A row is a value that can
// be replaced, so a row has to be a thing.
//
// Nothing is freed. A Shalimar program runs once and stops, the app's
// interpreter has a garbage collector where this has none, and a scheme that
// freed some of this would have to decide who owns a row that has been handed
// to a function - which is a question the language does not otherwise ask.
// The cost is a program that builds strings in a long loop; that is written
// down in the README rather than left to be discovered.
#include "Internal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace shm {
namespace {

void *zeroed(size_t bytes) {
    void *memory = std::calloc(bytes ? bytes : 1, 1);
    if (!memory) fail("Out of memory");
    return memory;
}

size_t elementBytes(int32_t element) {
    switch (element) {
    case KindInt:  return sizeof(int32_t);
    case KindReal: return sizeof(double);
    case KindChar: return sizeof(unsigned char);
    default:       return sizeof(Array *);
    }
}

Array *allocate(int32_t element, int32_t count) {
    Array *array = static_cast<Array *>(zeroed(sizeof(Array)));
    array->count = count;
    array->element = element;
    array->data = zeroed(static_cast<size_t>(count) * elementBytes(element));
    return array;
}

void checkExtent(int64_t extent) {
    if (extent >= 1) return;
    char message[80];
    std::snprintf(message, sizeof message, "Array size must be 1 or more, got %lld",
                  static_cast<long long>(extent));
    fail(message);
}

Array *build(int32_t element, int32_t rank, const int64_t *dims) {
    checkExtent(dims[0]);
    const int32_t count = static_cast<int32_t>(dims[0]);
    if (rank == 1) return allocate(element, count);

    Array *array = allocate(KindRef, count);
    for (int32_t i = 0; i < count; ++i) {
        array->refs()[i] = build(element, rank - 1, dims + 1);
    }
    return array;
}

void bounds(const Array *array, int32_t index) {
    if (index >= 0 && index < array->count) return;
    char message[80];
    std::snprintf(message, sizeof message, "Index %d out of range 0...%d",
                  static_cast<int>(index), static_cast<int>(array->count - 1));
    fail(message);
}

// Resets every element to the zero of its own type, in place and at any rank,
// keeping the storage and the extents. Clearing is not retyping.
void clear(Array *array) {
    if (array->element == KindRef) {
        for (int32_t i = 0; i < array->count; ++i) {
            if (array->refs()[i]) clear(array->refs()[i]);
        }
        return;
    }
    std::memset(array->data, 0,
                static_cast<size_t>(array->count) * elementBytes(array->element));
}

// Copies as much of `source` as fits, leaving the rest of `destination` as it
// was found - which the caller has already cleared. For a char array the last
// slot is reserved for the terminator, so 'char s[4]' holds three characters.
void fit(Array *destination, const Array *source) {
    if (!destination || !source) return;
    const bool text = destination->element == KindChar;
    const int32_t capacity = text ? (destination->count > 0 ? destination->count - 1 : 0)
                                  : destination->count;
    const int32_t n = capacity < source->count ? capacity : source->count;

    for (int32_t i = 0; i < n; ++i) {
        switch (destination->element) {
        case KindInt:  destination->ints()[i]  = source->element == KindReal
                           ? static_cast<int32_t>(source->reals()[i]) : source->ints()[i];
                       break;
        case KindReal: destination->reals()[i] = source->element == KindInt
                           ? static_cast<double>(source->ints()[i]) : source->reals()[i];
                       break;
        case KindChar: destination->chars()[i] = source->chars()[i]; break;
        default:       fit(destination->refs()[i], source->refs()[i]); break;
        }
    }
}

}  // namespace

int32_t textLength(const Array *array) {
    if (!array || array->element != KindChar) return 0;
    for (int32_t i = 0; i < array->count; ++i) {
        if (array->chars()[i] == 0) return i;
    }
    return array->count;
}

}  // namespace shm

using namespace shm;

extern "C" {

ShmArray *shm_array_make(int32_t element, int32_t rank, const int64_t *dims) {
    return reinterpret_cast<ShmArray *>(build(element, rank, dims));
}

// A string literal is the text plus a terminating char(0), so its capacity is
// one more than its length.
ShmArray *shm_array_from_text(const char *bytes, int32_t length) {
    Array *array = allocate(KindChar, length + 1);
    std::memcpy(array->chars(), bytes, static_cast<size_t>(length));
    return reinterpret_cast<ShmArray *>(array);
}

// Walks down 'axis' levels taking the first element at each, then counts. An
// axis the array does not have answers -1 rather than failing, so asking a
// vector for its columns is a legal question with a recognizable answer. A
// scalar is a different mistake and never reaches here.
int32_t shm_array_dim(const ShmArray *handle, int32_t axis) {
    const Array *array = reinterpret_cast<const Array *>(handle);
    if (axis < 0 || !array) return -1;
    if (axis == 0) return array->count;
    if (array->element != KindRef || array->count == 0) return -1;
    return shm_array_dim(reinterpret_cast<const ShmArray *>(array->refs()[0]), axis - 1);
}

int32_t shm_get_int(const ShmArray *handle, int32_t index) {
    const Array *array = reinterpret_cast<const Array *>(handle);
    bounds(array, index);
    return array->ints()[index];
}

double shm_get_real(const ShmArray *handle, int32_t index) {
    const Array *array = reinterpret_cast<const Array *>(handle);
    bounds(array, index);
    return array->reals()[index];
}

int32_t shm_get_char(const ShmArray *handle, int32_t index) {
    const Array *array = reinterpret_cast<const Array *>(handle);
    bounds(array, index);
    return array->chars()[index];
}

ShmArray *shm_get_ref(const ShmArray *handle, int32_t index) {
    const Array *array = reinterpret_cast<const Array *>(handle);
    bounds(array, index);
    return reinterpret_cast<ShmArray *>(array->refs()[index]);
}

void shm_set_int(ShmArray *handle, int32_t index, int32_t value) {
    Array *array = reinterpret_cast<Array *>(handle);
    bounds(array, index);
    array->ints()[index] = value;
}

void shm_set_real(ShmArray *handle, int32_t index, double value) {
    Array *array = reinterpret_cast<Array *>(handle);
    bounds(array, index);
    array->reals()[index] = value;
}

void shm_set_char(ShmArray *handle, int32_t index, int32_t value) {
    Array *array = reinterpret_cast<Array *>(handle);
    bounds(array, index);
    array->chars()[index] = static_cast<unsigned char>(value);
}

// Element assignment replaces the row. That is what makes an array's
// dimensions measured rather than declared.
void shm_set_ref(ShmArray *handle, int32_t index, ShmArray *value) {
    Array *array = reinterpret_cast<Array *>(handle);
    bounds(array, index);
    array->refs()[index] = reinterpret_cast<Array *>(value);
}

void shm_array_fill(ShmArray *destination, const ShmArray *source) {
    Array *to = reinterpret_cast<Array *>(destination);
    const Array *from = reinterpret_cast<const Array *>(source);
    if (!to || !from) return;
    // Cleared first, so the literal is the array's whole new value rather
    // than a patch over the old one. Absence then means the same thing
    // however it arises - a gap inside the literal, or a literal that stops
    // short of the extents.
    clear(to);
    fit(to, from);
}

ShmArray *shm_text_concat(const ShmArray *a, const ShmArray *b) {
    const Array *left = reinterpret_cast<const Array *>(a);
    const Array *right = reinterpret_cast<const Array *>(b);
    const int32_t na = textLength(left);
    const int32_t nb = textLength(right);
    Array *joined = allocate(KindChar, na + nb + 1);
    if (na) std::memcpy(joined->chars(), left->chars(), static_cast<size_t>(na));
    if (nb) std::memcpy(joined->chars() + na, right->chars(), static_cast<size_t>(nb));
    return reinterpret_cast<ShmArray *>(joined);
}

// Negative, zero or positive, like C - the compiler turns that into whichever
// of the six answers the operator wanted. Ordering is by character code, so
// every capital sorts before every lower-case letter.
int32_t shm_text_compare(const ShmArray *a, const ShmArray *b) {
    const Array *left = reinterpret_cast<const Array *>(a);
    const Array *right = reinterpret_cast<const Array *>(b);
    const int32_t na = textLength(left);
    const int32_t nb = textLength(right);
    const int32_t n = na < nb ? na : nb;
    for (int32_t i = 0; i < n; ++i) {
        const unsigned char x = left->chars()[i];
        const unsigned char y = right->chars()[i];
        if (x != y) return x < y ? -1 : 1;
    }
    if (na == nb) return 0;
    return na < nb ? -1 : 1;
}

}  // extern "C"
