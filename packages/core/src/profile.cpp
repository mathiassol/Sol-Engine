#include <engine/core/profile.hpp>

#include <engine/core/clock.hpp>

#include <cstring>

namespace engine {

namespace {

// Bounds two independent things, and both overrun silently. Audit finding S6.
//
// 1. **Nesting depth.** Past kMaxScopes, begin_scope drops the push but
//    end_scope still decrements, so the pop consumes a *different* scope's
//    start_ entry: the time lands on a scope that is still open, and the
//    imbalance cascades through the rest of the frame. Deepest actual nesting
//    is 3 - frame -> render -> extract - so this is not reachable today. If a
//    future scope tree gets near 8, fix the imbalance (count dropped pushes and
//    skip the matching pops) rather than just raising the cap.
//
// 2. **Distinct scope names per frame.** accumulate() keys slots by name and
//    falls off its loop when all kMaxScopes are taken, so the next new name is
//    dropped and scope_ms() returns 0.0 for it - which the F3 overlay renders
//    as "0.0", indistinguishable from a scope that genuinely took no time.
//
//    This is the near limit: **7 of the 8 slots are in use** (frame,
//    poll_events, fixed_update, update, render, extract, execute). One more
//    ENGINE_PROFILE_SCOPE with a new name fills it; the one after that reads
//    zero forever. Raise kMaxScopes when adding a scope.
constexpr u32 kMaxScopes = 8;

class FrameProfiler final : public IProfiler {
public:
    void begin_frame() override {
        for (u32 i = 0; i < kMaxScopes; ++i) {
            last_[i] = current_[i];
            current_[i] = {};
        }
        depth_ = 0;
    }

    void begin_scope(const char* name) override {
        if (depth_ >= kMaxScopes || !name) {
            return;
        }
        stack_[depth_] = name;
        start_[depth_] = clock_.now();
        ++depth_;
    }

    void end_scope(const char* name) override {
        if (depth_ == 0) {
            return;
        }
        --depth_;
        const f32 ms = static_cast<f32>((clock_.now() - start_[depth_]) * 1000.0);
        accumulate(current_, name ? name : stack_[depth_], ms);
    }

    f32 scope_ms(const char* name) const override {
        return find_ms(last_, name);
    }

private:
    struct Slot {
        const char* name = nullptr;
        f32 ms = 0.f;
    };

    static void accumulate(Slot* slots, const char* name, f32 ms) {
        if (!name) {
            return;
        }
        for (u32 i = 0; i < kMaxScopes; ++i) {
            if (slots[i].name && std::strcmp(slots[i].name, name) == 0) {
                slots[i].ms += ms;
                return;
            }
            if (!slots[i].name) {
                slots[i].name = name;
                slots[i].ms = ms;
                return;
            }
        }
    }

    static f32 find_ms(const Slot* slots, const char* name) {
        if (!name) {
            return 0.f;
        }
        for (u32 i = 0; i < kMaxScopes; ++i) {
            if (slots[i].name && std::strcmp(slots[i].name, name) == 0) {
                return slots[i].ms;
            }
        }
        return 0.f;
    }

    Clock clock_;
    Slot current_[kMaxScopes]{};
    Slot last_[kMaxScopes]{};
    const char* stack_[kMaxScopes]{};
    f64 start_[kMaxScopes]{};
    u32 depth_ = 0;
};

IProfiler* g_profiler = nullptr;
FrameProfiler g_frame_profiler;

} // namespace

void set_profiler(IProfiler* profiler) {
    g_profiler = profiler;
}

IProfiler* profiler() {
    return g_profiler ? g_profiler : &g_frame_profiler;
}

void profiler_begin_frame() {
    profiler()->begin_frame();
}

f32 profiler_scope_ms(const char* name) {
    return profiler()->scope_ms(name);
}

ProfileScope::ProfileScope(const char* name) : name_(name) {
    profiler()->begin_scope(name_);
}

ProfileScope::~ProfileScope() {
    profiler()->end_scope(name_);
}

} // namespace engine
