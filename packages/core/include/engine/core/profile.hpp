#pragma once

namespace engine {

class IProfiler {
public:
    virtual ~IProfiler() = default;
    virtual void begin_scope(const char* name) = 0;
    virtual void end_scope(const char* name) = 0;
};

void set_profiler(IProfiler* profiler);
IProfiler* profiler();

class ProfileScope {
public:
    explicit ProfileScope(const char* name);
    ~ProfileScope();

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;

private:
    const char* name_;
};

} // namespace engine

#define ENGINE_PROFILE_SCOPE(name) ::engine::ProfileScope ENGINE_PROFILE_CONCAT(_engine_scope_, __LINE__)(name)
#define ENGINE_PROFILE_CONCAT(a, b) ENGINE_PROFILE_CONCAT_IMPL(a, b)
#define ENGINE_PROFILE_CONCAT_IMPL(a, b) a##b
