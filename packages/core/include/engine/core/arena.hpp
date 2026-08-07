#pragma once

#include <engine/core/types.hpp>

#include <cstddef>

namespace engine {

// Linear bump allocator. Reset per frame for temporaries; no individual free.
class Arena {
public:
    explicit Arena(usize capacity_bytes);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    void* alloc(usize size, usize alignment = alignof(std::max_align_t));
    void  reset();

    usize used() const { return offset_; }
    usize capacity() const { return capacity_; }

private:
    u8*    buffer_   = nullptr;
    usize  capacity_ = 0;
    usize  offset_   = 0;
};

} // namespace engine
