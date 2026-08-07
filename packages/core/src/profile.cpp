#include <engine/core/profile.hpp>

namespace engine {

namespace {

class NullProfiler final : public IProfiler {
public:
    void begin_scope(const char*) override {}
    void end_scope(const char*) override {}
};

IProfiler* g_profiler = nullptr;
NullProfiler g_null_profiler;

} // namespace

void set_profiler(IProfiler* profiler) {
    g_profiler = profiler;
}

IProfiler* profiler() {
    return g_profiler ? g_profiler : &g_null_profiler;
}

ProfileScope::ProfileScope(const char* name) : name_(name) {
    profiler()->begin_scope(name_);
}

ProfileScope::~ProfileScope() {
    profiler()->end_scope(name_);
}

} // namespace engine
