#include <engine/core/profile.hpp>

#include <engine/core/clock.hpp>

#include <cstring>

namespace engine {

namespace {

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
