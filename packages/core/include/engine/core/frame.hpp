#pragma once

#include <engine/core/clock.hpp>
#include <engine/core/types.hpp>

namespace engine {

struct FrameContext {
    u64 frame_index   = 0;
    u8  frame_slot    = 0;   // cycles 0..frames_in_flight-1
    f64 time          = 0.0;   // total elapsed seconds
    f32 delta         = 0.0f;  // variable frame delta (clamped)
    f32 fixed_delta   = 0.0f;  // constant simulation step
    f32 alpha         = 0.0f;  // interpolation [0,1) between fixed steps
    u32 fixed_steps   = 0;     // fixed steps consumed this frame
};

struct FrameTimerConfig {
    f32 fixed_timestep  = 1.0f / 60.0f;
    f32 max_delta       = 0.25f;  // spiral-of-death clamp
    u8  frames_in_flight = 3;
};

class FrameTimer {
public:
    explicit FrameTimer(FrameTimerConfig config = {});

    // Call once per frame before any phase runs.
    FrameContext begin_frame();

    const FrameContext& context() const { return context_; }

    // Drain fixed-step accumulator; call fixed_update() each iteration.
    bool consume_fixed_step();

private:
    FrameTimerConfig config_;
    Clock clock_;
    FrameContext context_;
    f32 accumulator_ = 0.0f;
};

} // namespace engine
