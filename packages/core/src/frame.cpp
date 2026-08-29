#include <engine/core/frame.hpp>

namespace engine {

FrameTimer::FrameTimer(FrameTimerConfig config) : config_(config) {
    context_.fixed_delta = config_.fixed_timestep;
}

FrameContext FrameTimer::begin_frame() {
    f32 raw_dt = clock_.tick();
    if (raw_dt > config_.max_delta) {
        raw_dt = config_.max_delta;
    }

    context_.frame_index++;
    context_.time   = clock_.now();
    context_.delta  = raw_dt;
    context_.fixed_delta = config_.fixed_timestep;
    context_.fixed_steps = 0;

    if (config_.frames_in_flight > 0) {
        context_.cpu_frame_slot = static_cast<u8>(
            context_.frame_index % config_.frames_in_flight);
    }

    accumulator_ += raw_dt;
    return context_;
}

bool FrameTimer::consume_fixed_step() {
    // A zero or negative timestep would make this loop forever and divide by
    // zero below. FrameTimerConfig is public, so guard rather than assume.
    if (!(config_.fixed_timestep > 0.f)) {
        context_.alpha = 0.f;
        return false;
    }

    if (context_.fixed_steps >= config_.max_steps_per_frame) {
        // Give up on catching all the way up. Discard the backlog so the next
        // frame starts clean instead of inheriting an ever-growing debt.
        accumulator_ = 0.f;
        context_.alpha = 0.f;
        return false;
    }

    if (accumulator_ < config_.fixed_timestep) {
        context_.alpha = accumulator_ / config_.fixed_timestep;
        return false;
    }

    accumulator_ -= config_.fixed_timestep;
    context_.fixed_steps++;
    context_.alpha = accumulator_ / config_.fixed_timestep;
    return true;
}

} // namespace engine
