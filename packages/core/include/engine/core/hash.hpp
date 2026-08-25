#pragma once

#include <engine/core/types.hpp>

#include <span>
#include <string_view>

namespace engine {

constexpr u64 kFnvOffset64 = 14695981039346656037ull;
constexpr u64 kFnvPrime64  = 1099511628211ull;

inline u64 fnv1a64(std::string_view data) {
    u64 hash = kFnvOffset64;
    for (unsigned char byte : data) {
        hash ^= byte;
        hash *= kFnvPrime64;
    }
    return hash;
}

inline u64 fnv1a64(std::span<const u8> data) {
    u64 hash = kFnvOffset64;
    for (u8 byte : data) {
        hash ^= byte;
        hash *= kFnvPrime64;
    }
    return hash;
}

} // namespace engine
