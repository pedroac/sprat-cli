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

} // namespace sprat::core
