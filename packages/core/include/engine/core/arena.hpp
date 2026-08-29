#pragma once

#include <engine/core/types.hpp>

#include <cstddef>
#include <new>

namespace engine {

// Linear bump allocator. Reset per frame for temporaries; no individual free.
//
// Exhaustion is a normal outcome, not a programmer error: how much a frame
// needs depends on the scene. Every allocator here returns nullptr when the
// arena is full and logs once per reset, so a caller drops work for the frame
// instead of taking the process down with it. Callers must check.
class Arena {
public:
    explicit Arena(usize capacity_bytes);
    ~Arena();

    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;

    // nullptr when the arena cannot fit `size`.
    [[nodiscard]] void* alloc(usize size, usize alignment = alignof(std::max_align_t));
    void  reset();

    template<typename T>
    [[nodiscard]] T* push() {
        void* memory = alloc(sizeof(T), alignof(T));
        return memory ? new (memory) T{} : nullptr;
    }

    template<typename T>
    [[nodiscard]] T* push_n(usize count) {
        if (count == 0) {
            return nullptr;
        }
        // sizeof(T) * count wraps for a large count - which is reachable when
        // count comes from a file or a scene - and would then pass the
        // capacity check and overrun the buffer during construction below.
        if (count > (~usize{0}) / sizeof(T)) {
            return nullptr;
        }
        T* memory = static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
        if (!memory) {
            return nullptr;
        }
        for (usize i = 0; i < count; ++i) {
            new (memory + i) T{};
        }
        return memory;
    }

    usize used() const { return offset_; }
    usize capacity() const { return capacity_; }
    // Cleared by reset(). Lets a caller report a dropped frame once.
    bool overflowed() const { return overflowed_; }

private:
    u8*    buffer_   = nullptr;
    usize  capacity_ = 0;
    usize  offset_   = 0;
    bool   overflowed_ = false;
};

} // namespace engine
