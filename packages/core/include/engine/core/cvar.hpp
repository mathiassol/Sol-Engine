#pragma once

#include <engine/core/types.hpp>

#include <string>
#include <string_view>

namespace engine {

enum class CvarType : u8 { Bool, Int, Float, String };

// Higher wins. A write is accepted only when its source is at least the source
// that last wrote the cvar, so the engine may load config.cfg after the
// command line and --set still wins.
enum class CvarSource : u8 { Default = 0, File = 1, CommandLine = 2, Code = 3 };

enum class CvarSetResult : u8 {
    Applied,  // parsed and stored
    Invalid,  // the text does not parse as this cvar's type
    Ignored,  // parsed, but a higher-precedence source already owns the value
};

const char* cvar_type_name(CvarType type);
const char* cvar_source_name(CvarSource source);

// A named knob. Declare one at file scope next to the code that reads it:
//
//   namespace { engine::Cvar cv_vsync{"r.vsync", true, "Present with vsync"}; }
//
// Construction registers it. Not thread-safe: drive it from startup and the
// main thread only.
class Cvar {
public:
    Cvar(const char* name, bool value, const char* help);
    Cvar(const char* name, i32 value, const char* help);
    Cvar(const char* name, f32 value, const char* help);
    Cvar(const char* name, const char* value, const char* help);

    Cvar(const Cvar&) = delete;
    Cvar& operator=(const Cvar&) = delete;

    const char* name() const { return name_; }
    const char* help() const { return help_; }
    CvarType    type() const { return type_; }
    CvarSource  source() const { return source_; }

    // Reading the wrong type is a programming error, not a config error: these
    // assert. ENGINE_ASSERT is always active and assert_fail is [[noreturn]],
    // so a mismatch aborts rather than returning a wrong value.
    bool             as_bool() const;
    i32              as_int() const;
    f32              as_float() const;
    std::string_view as_string() const;

    CvarSetResult set(std::string_view text, CvarSource source);

private:
    const char* name_;
    const char* help_;
    CvarType    type_;
    CvarSource  source_ = CvarSource::Default;
    bool        bool_   = false;
    i32         int_    = 0;
    f32         float_  = 0.f;
    std::string string_;
};

struct CvarApplyStats {
    usize applied = 0;
    usize unknown = 0;  // no cvar by that name
    usize invalid = 0;  // bad value, or a malformed line
    usize ignored = 0;  // a higher-precedence source already owns the value
};

// Linear scan. There are a few dozen knobs; a map would cost more than it saves.
Cvar* find_cvar(std::string_view name);
usize cvar_count();
Cvar* cvar_at(usize index);  // nullptr when index is out of range

CvarApplyStats apply_cvar_text(std::string_view text, CvarSource source);
CvarApplyStats apply_cvar_args(int argc, const char* const* argv);

} // namespace engine
