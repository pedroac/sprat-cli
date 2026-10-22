#pragma once

#include <cstddef>
#include <limits>

namespace sprat::core {

// Multiplies a * b into out. Returns false (leaving out untouched) on overflow.
inline bool checked_mul_size_t(size_t a, size_t b, size_t& out) {
    if (a == 0 || b <= std::numeric_limits<size_t>::max() / a) {
        out = a * b;
        return true;
    }
    return false;
}

// Adds a + b into out. Returns false (leaving out untouched) on overflow.
inline bool checked_add_int(int a, int b, int& out) {
    if (b > 0 && a > std::numeric_limits<int>::max() - b) {
        return false;
    }
    if (b < 0 && a < std::numeric_limits<int>::min() - b) {
        return false;
    }
    out = a + b;
    return true;
}

} // namespace sprat::core
