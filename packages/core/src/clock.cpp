#include <engine/core/clock.hpp>

#include <chrono>

namespace engine {

namespace {
f64 steady_now() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<f64>(clock::now().time_since_epoch()).count();
}
} // namespace

Clock::Clock() : origin_(steady_now()), last_(origin_) {}

f64 Clock::now() const {
    return steady_now() - origin_;
}

f32 Clock::tick() {
    f64 t = steady_now();
    f32 dt = static_cast<f32>(t - last_);
    last_ = t;
    return dt;
}

f32 Clock::peek_delta() const {
    return static_cast<f32>(steady_now() - last_);
}

} // namespace engine
