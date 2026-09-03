#pragma once

#include <engine/core/clock.hpp>
#include <engine/core/types.hpp>

namespace engine {

struct FrameContext {
    u64 frame_index   = 0;
    u8  cpu_frame_slot = 0; // CPU timer ring 0..frames_in_flight-1; not the RHI's slot
    f64 time          = 0.0;   // total elapsed seconds
    f32 delta         = 0.0f;  // variable frame delta (clamped)
    f32 fixed_delta   = 0.0f;  // constant simulation step
    f32 alpha         = 0.0f;  // interpolation [0,1) between fixed steps
    u32 fixed_steps   = 0;     // fixed steps consumed this frame
};

struct FrameTimerConfig {
    f32 fixed_timestep  = 1.0f / 60.0f;
    f32 max_delta       = 0.25f;  // clamps one frame's input delta
    // Hard cap on fixed steps drained per frame. max_delta alone does not stop
    // a spiral: if a step costs more wall time than it simulates, the
    // accumulator grows every frame and the loop takes longer every frame.
    // Hitting this cap drops simulated time (the world runs slow) instead of
    // freezing, which is recoverable.
    u32 max_steps_per_frame = 16;
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
