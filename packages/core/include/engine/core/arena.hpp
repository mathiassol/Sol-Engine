#pragma once

#include <engine/core/types.hpp>

#include <cstddef>
#include <new>

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

    template<typename T>
    T* push() {
        void* memory = alloc(sizeof(T), alignof(T));
        return new (memory) T{};
    }

    template<typename T>
    T* push_n(usize count) {
        if (count == 0) {
            return nullptr;
        }
        T* memory = static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
        for (usize i = 0; i < count; ++i) {
            new (memory + i) T{};
        }
        return memory;
    }

    usize used() const { return offset_; }
    usize capacity() const { return capacity_; }

private:
    u8*    buffer_   = nullptr;
    usize  capacity_ = 0;
    usize  offset_   = 0;
};

} // namespace engine
