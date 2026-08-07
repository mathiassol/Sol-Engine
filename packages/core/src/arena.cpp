#include <engine/core/arena.hpp>

#include <cstdlib>
#include <cstring>
#include <new>

namespace engine {

namespace {
usize align_up(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace

Arena::Arena(usize capacity_bytes) : capacity_(capacity_bytes) {
    buffer_ = static_cast<u8*>(std::malloc(capacity_));
}

Arena::~Arena() {
    std::free(buffer_);
}

void* Arena::alloc(usize size, usize alignment) {
    if (!buffer_ || size == 0) return nullptr;

    usize aligned = align_up(offset_, alignment);
    if (aligned + size > capacity_) return nullptr;

    void* ptr = buffer_ + aligned;
    offset_ = aligned + size;
    return ptr;
}

void Arena::reset() {
    offset_ = 0;
}

} // namespace engine
