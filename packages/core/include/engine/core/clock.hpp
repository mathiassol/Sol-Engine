#pragma once

#include <engine/core/types.hpp>

namespace engine {

// Monotonic high-resolution clock. Uses std::chrono::steady_clock internally.
class Clock {
public:
    Clock();

    // Seconds since construction, never decreases.
    f64 now() const;

    // Seconds since last tick(); updates the internal marker.
    f32 tick();

    // Peek delta without updating the marker.
    f32 peek_delta() const;

private:
    f64 origin_;
    f64 last_;
};

} // namespace engine
