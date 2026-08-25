#pragma once

#include <engine/core/types.hpp>

namespace engine::debug {

struct FrameStats {
    f32 fps = 0.f;
    f32 frame_ms = 0.f;
    f32 cpu_ms = 0.f;
    f32 poll_ms = 0.f;
    f32 extract_ms = 0.f;
    f32 execute_ms = 0.f;
    f32 gpu_ms = 0.f;
    const char* aa = "";
};

class FrameStatsTracker {
public:
    void update(f32 delta_seconds);
    const FrameStats& stats() const { return stats_; }

private:
    FrameStats stats_{};
    f32 smoothed_fps_ = 0.f;
};

} // namespace engine::debug
