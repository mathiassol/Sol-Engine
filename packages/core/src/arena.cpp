#include <engine/core/arena.hpp>
#include <engine/core/assert.hpp>
#include <engine/core/log.hpp>

#include <cstdio>
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

    const usize aligned = align_up(offset_, alignment);
    // Exhaustion is a scene-size outcome, not a bug. Drop the allocation and
    // let the caller skip its work; taking the process down loses the frame
    // *and* every frame after it.
    if (aligned < offset_ || aligned > capacity_ || size > capacity_ - aligned) {
        if (!overflowed_) {
            overflowed_ = true;
            char message[160];
            std::snprintf(message, sizeof(message),
                "Frame arena exhausted: %zu bytes used of %zu, %zu more requested. "
                "Work is being dropped this frame.",
                offset_, capacity_, size);
            log(LogLevel::Error, LogChannel::General, message);
        }
        return nullptr;
    }

    void* ptr = buffer_ + aligned;
    offset_ = aligned + size;
    return ptr;
}

void Arena::reset() {
    offset_ = 0;
    overflowed_ = false;
}

} // namespace engine
