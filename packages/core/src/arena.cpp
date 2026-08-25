#include <engine/core/arena.hpp>
#include <engine/core/assert.hpp>

#include <cstdlib>

namespace engine {

namespace {
usize align_up(usize value, usize alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}
} // namespace

Arena::Arena(usize capacity_bytes) : capacity_(capacity_bytes) {
    ENGINE_ASSERT(capacity_bytes > 0);
    buffer_ = static_cast<u8*>(std::malloc(capacity_));
    ENGINE_ASSERT_MSG(buffer_ != nullptr, "Arena malloc failed");
}

Arena::~Arena() {
    std::free(buffer_);
}

void* Arena::alloc(usize size, usize alignment) {
    ENGINE_ASSERT(buffer_ != nullptr);
    ENGINE_ASSERT(size > 0);
    ENGINE_ASSERT(alignment > 0 && (alignment & (alignment - 1)) == 0);

    usize aligned = align_up(offset_, alignment);
    ENGINE_ASSERT_MSG(aligned + size <= capacity_, "Arena out of memory");

    void* ptr = buffer_ + aligned;
    offset_ = aligned + size;
    return ptr;
}

void Arena::reset() {
    offset_ = 0;
}

} // namespace engine
