#pragma once

#include <cstdint>
#include <cstring>

namespace sprat::core {

// FNV-1a 64-bit hash function
// Reference: http://www.isthe.com/chongo/tech/comp/fnv/
constexpr uint64_t k_fnv1a_offset_basis = 0xCBF29CE484222325ULL;

// Continues a hash over more bytes, so non-contiguous data (e.g. image rows) can be hashed.
inline uint64_t fnv1a_update(uint64_t hash, const unsigned char* data, size_t len) {
    constexpr uint64_t FNV_64_PRIME = 0x100000001B3ULL;
    for (size_t i = 0; i < len; ++i) {
        hash ^= static_cast<uint64_t>(data[i]);
        hash *= FNV_64_PRIME;
    }
    return hash;
}

inline uint64_t fnv1a_hash(const unsigned char* data, size_t len) {
    return fnv1a_update(k_fnv1a_offset_basis, data, len);
}

} // namespace sprat::core
