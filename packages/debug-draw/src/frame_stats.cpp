#include <engine/debug/frame_stats.hpp>

namespace engine::debug {

void FrameStatsTracker::update(f32 delta_seconds) {
    if (delta_seconds <= 0.f) {
        return;
    }

    const f32 instant_fps = 1.f / delta_seconds;
    if (smoothed_fps_ <= 0.f) {
        smoothed_fps_ = instant_fps;
    } else {
        smoothed_fps_ = smoothed_fps_ * 0.9f + instant_fps * 0.1f;
    }

    stats_.fps     = smoothed_fps_;
    stats_.frame_ms = delta_seconds * 1000.f;
    stats_.cpu_ms  = stats_.frame_ms;
}

} // namespace engine::debug
