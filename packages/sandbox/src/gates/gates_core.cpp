#include "../sandbox_common.hpp"
#include "gate_registry.hpp"

#include <engine/reflect/field.hpp>
#include <engine/math/vec2.hpp>
#include <engine/math/vec3.hpp>
#include <engine/math/vec4.hpp>
#include <engine/math/mat4.hpp>

// Core, config and identity gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

namespace {

// A stand-in for the kind of POD a component will be. Deliberately has mixed
// field widths and interior padding, which is what makes the descriptor checks
// meaningful. Anonymous namespace because only this file needs the type, and
// `namespace sandbox` spans ten files.
struct ReflectProbe {
    engine::math::Vec3 pos;
    engine::f32 radius;
    engine::u32 flags;
    bool visible;
};

} // namespace

bool run_gate_registry_gate() {
    // The kGates table's own consistency, at runtime. Invariant 15 checks the
    // table against the definitions in source; this checks the table against
    // itself, and runs headless, so a Cpu classification that cannot actually
    // be invoked is caught where it would be used.
    engine::u32 cpu = 0;
    engine::u32 gpu = 0;
    bool kinds_ok = true;
    bool names_unique = true;
    for (engine::usize i = 0; i < kGateCount; ++i) {
        const GateEntry& e = kGates[i];
        if (e.kind == GateKind::Cpu) {
            ++cpu;
            kinds_ok = kinds_ok && e.cpu_fn != nullptr;
        } else {
            ++gpu;
            kinds_ok = kinds_ok && e.cpu_fn == nullptr;
        }
        for (engine::usize j = i + 1; j < kGateCount; ++j) {
            if (std::string_view(e.name) == kGates[j].name) {
                names_unique = false;
            }
        }
    }
    // This gate must be in the table it is checking. A registry that has
    // forgotten the gate verifying it is the one failure this cannot otherwise
    // see.
    bool self_present = false;
    for (engine::usize i = 0; i < kGateCount; ++i) {
        if (std::string_view(kGates[i].name) == "run_gate_registry_gate") {
            self_present = kGates[i].kind == GateKind::Cpu;
        }
    }

    const bool passed = kinds_ok && names_unique && self_present && cpu + gpu == kGateCount
        && cpu > 0 && gpu > 0;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Gate registry gate: total=%u cpu=%u gpu=%u kinds_consistent=%s names_unique=%s "
        "self_registered=%s (%s)",
        static_cast<engine::u32>(kGateCount), cpu, gpu, kinds_ok ? "yes" : "NO",
        names_unique ? "yes" : "NO", self_present ? "yes" : "NO", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_file_log_gate() {
    std::error_code ec;
    const std::filesystem::path root =
        std::filesystem::temp_directory_path(ec) / "sol_file_log_gate";
    std::filesystem::remove_all(root, ec);

    const std::filesystem::path log_dir = root / "logs";
    const std::filesystem::path current = log_dir / "log.txt";
    const std::filesystem::path previous = log_dir / "log.prev.txt";

    // Run one: three records, then close by destroying the sink.
    bool created = false;
    {
        auto sink = engine::create_file_logger(log_dir.string());
        created = sink != nullptr;
        if (sink) {
            sink->log(engine::LogLevel::Info, engine::LogChannel::General, "first line");
            sink->log(engine::LogLevel::Warn, engine::LogChannel::Assets, "second line");
            sink->log(engine::LogLevel::Error, engine::LogChannel::Render, "third line");
        }
    }

    const std::string run_one = read_text_file(current);
    const bool header_ok = run_one.find("Sol Engine session log") != std::string::npos
        && run_one.find("started ") != std::string::npos;
    const bool lines_ok = run_one.find("[INFO][general] first line") != std::string::npos
        && run_one.find("[WARN][assets] second line") != std::string::npos
        && run_one.find("[ERROR][render] third line") != std::string::npos;

    // Run two: rotates run one to log.prev.txt and starts fresh.
    {
        auto sink = engine::create_file_logger(log_dir.string());
        if (sink) {
            sink->log(engine::LogLevel::Info, engine::LogChannel::General, "fourth line");
        }
    }

    const std::string prev = read_text_file(previous);
    const std::string run_two = read_text_file(current);
    const bool rotated = !prev.empty() && !run_two.empty();
    const bool prev_intact = prev.find("[INFO][general] first line") != std::string::npos
        && prev.find("[ERROR][render] third line") != std::string::npos;
    const bool fresh = run_two.find("[INFO][general] fourth line") != std::string::npos
        && run_two.find("first line") == std::string::npos;

    // An undirectory: create_directories cannot make a directory under a file,
    // so this is a deterministic unwritable path on any platform.
    const std::filesystem::path blocker = root / "not_a_directory";
    {
        std::ofstream make_file(blocker);
        make_file << "x";
    }
    auto rejected_sink = engine::create_file_logger((blocker / "logs").string());
    const bool unwritable_rejected = rejected_sink == nullptr;

    std::filesystem::remove_all(root, ec);

    const bool passed = created && header_ok && lines_ok && rotated && prev_intact
        && fresh && unwritable_rejected;
    char message[224];
    std::snprintf(message, sizeof(message),
        "File log gate: created=%s header=%s lines=%s rotated=%s prev_intact=%s "
        "fresh=%s unwritable_rejected=%s (%s)",
        created ? "yes" : "no", header_ok ? "yes" : "no", lines_ok ? "yes" : "no",
        rotated ? "yes" : "no", prev_intact ? "yes" : "no", fresh ? "yes" : "no",
        unwritable_rejected ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_arena_gate() {

    engine::Arena arena(1024);

    engine::u8* first = arena.push_n<engine::u8>(512);
    const bool first_ok = first != nullptr && arena.used() == 512 && !arena.overflowed();

    // Past capacity: nullptr, flagged, and the arena stays usable.
    engine::u8* too_big = arena.push_n<engine::u8>(4096);
    const bool rejected = too_big == nullptr && arena.overflowed();
    const engine::usize used_after_reject = arena.used();

    // A rejected allocation must not consume the bump pointer.
    engine::u8* second = arena.push_n<engine::u8>(256);
    const bool still_usable = second != nullptr && used_after_reject == 512;

    // sizeof(T) * count wrapping used to pass the capacity check and then
    // overrun the buffer while constructing.
    struct Big { engine::u8 bytes[64]; };
    Big* overflowed = arena.push_n<Big>(~engine::usize{0} / 8);
    const bool overflow_rejected = overflowed == nullptr;

    arena.reset();
    const bool reset_ok = arena.used() == 0 && !arena.overflowed()
        && arena.push_n<engine::u8>(1024) != nullptr;

    const bool passed = first_ok && rejected && still_usable && overflow_rejected && reset_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Arena gate: alloc=%s over_capacity_rejected=%s offset_preserved=%s "
        "size_overflow_rejected=%s reset=%s (%s)",
        first_ok ? "yes" : "no", rejected ? "yes" : "no", still_usable ? "yes" : "no",
        overflow_rejected ? "yes" : "no", reset_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_frame_timer_gate() {
    // A tiny timestep makes any real frame delta demand far more steps than
    // the cap allows, which is exactly the spiral condition: a step that costs
    // more wall time than it simulates.
    engine::FrameTimerConfig config{};
    config.fixed_timestep = 1.e-5f;      // 10 us
    config.max_steps_per_frame = 8;
    engine::FrameTimer timer(config);

    // Real elapsed time is what drives the accumulator, so the gate has to
    // actually burn some. 20 ms against a 10 us step demands ~2000 steps -
    // far past the cap. Without this the deltas are ~0, the loop runs zero
    // times, and the gate asserts 0 <= 8, which proves nothing.
    constexpr auto kBurn = std::chrono::milliseconds(20);

    std::this_thread::sleep_for(kBurn);
    timer.begin_frame();
    engine::u32 first_steps = 0;
    while (timer.consume_fixed_step()) {
        ++first_steps;
    }

    std::this_thread::sleep_for(kBurn);
    timer.begin_frame();
    engine::u32 second_steps = 0;
    while (timer.consume_fixed_step()) {
        ++second_steps;
    }

    // Demand far exceeded the cap, so both frames must land exactly on it.
    const bool capped = first_steps == config.max_steps_per_frame
        && second_steps == config.max_steps_per_frame;
    // The backlog is discarded at the cap, so frame two is no worse than frame
    // one. That monotonic growth is what made the freeze unrecoverable.
    const bool no_growth = second_steps <= first_steps;

    // A zero timestep must terminate rather than loop forever.
    engine::FrameTimerConfig degenerate{};
    degenerate.fixed_timestep = 0.f;
    engine::FrameTimer zero_timer(degenerate);
    zero_timer.begin_frame();
    const bool zero_safe = !zero_timer.consume_fixed_step();

    const bool passed = capped && no_growth && zero_safe;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Frame timer gate: steps=%u,%u cap=%u capped=%s no_growth=%s zero_step_safe=%s (%s)",
        first_steps, second_steps, config.max_steps_per_frame,
        capped ? "yes" : "no", no_growth ? "yes" : "no", zero_safe ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_math_guard_gate() {
    auto finite = [](const engine::math::Mat4& m) {
        for (int c = 0; c < 4; ++c) {
            const engine::math::Vec4& col = m.cols[c];
            if (!std::isfinite(col.x) || !std::isfinite(col.y) || !std::isfinite(col.z)
                || !std::isfinite(col.w)) {
                return false;
            }
        }
        return true;
    };

    // Zero fov, zero aspect (a minimised window), and near == far each used to
    // divide by zero straight into a constant buffer.
    const bool zero_fov = finite(engine::math::Mat4::perspective(0.f, 1.6f, 0.1f, 100.f));
    const bool zero_aspect = finite(engine::math::Mat4::perspective(1.0f, 0.f, 0.1f, 100.f));
    const bool equal_planes = finite(engine::math::Mat4::perspective(1.0f, 1.6f, 1.f, 1.f));
    // An empty scene reaches ortho() with a zero extent via sun-bounds fitting.
    const bool zero_extent = finite(engine::math::Mat4::ortho(0.f, 0.f, 0.f, 0.f, 0.f, 0.f));
    // A normal projection must still be exactly what it was.
    const engine::math::Mat4 normal = engine::math::Mat4::perspective(1.0f, 1.6f, 0.1f, 100.f);
    const bool unchanged = finite(normal) && normal.cols[1].y > 1.7f && normal.cols[1].y < 1.9f;

    const bool passed = zero_fov && zero_aspect && equal_planes && zero_extent && unchanged;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Math guard gate: zero_fov=%s zero_aspect=%s equal_planes=%s zero_extent=%s "
        "normal_proj=%.3f (%s)",
        zero_fov ? "finite" : "NaN", zero_aspect ? "finite" : "NaN",
        equal_planes ? "finite" : "NaN", zero_extent ? "finite" : "NaN",
        static_cast<double>(normal.cols[1].y), passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_cvar_gate(engine::platform::IFileSystem* fs, const std::string& scratch_dir) {
    using engine::CvarSetResult;
    using engine::CvarSource;

    const engine::usize count = engine::cvar_count();
    bool walked = false;
    for (engine::usize i = 0; i < count; ++i) {
        if (engine::cvar_at(i) == &cv_gate_prec) {
            walked = true;
        }
    }
    const bool registry_ok = count >= 18
        && walked
        && engine::find_cvar("gate.bool") == &cv_gate_bool
        && engine::find_cvar("gate.string") == &cv_gate_string
        && engine::find_cvar("gate.nope") == nullptr
        && engine::cvar_at(count) == nullptr;

    const bool types_ok =
        cv_gate_bool.set("on", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_bool.as_bool()
        && cv_gate_bool.set("0", CvarSource::Code) == CvarSetResult::Applied
        && !cv_gate_bool.as_bool()
        && cv_gate_bool.set("maybe", CvarSource::Code) == CvarSetResult::Invalid
        && !cv_gate_bool.as_bool()
        && cv_gate_int.set("-42", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_int.as_int() == -42
        && cv_gate_int.set("4.5", CvarSource::Code) == CvarSetResult::Invalid
        && cv_gate_int.set("", CvarSource::Code) == CvarSetResult::Invalid
        && cv_gate_float.set("-0.25", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_float.as_float() < -0.24f && cv_gate_float.as_float() > -0.26f
        && cv_gate_string.set("two words", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_string.as_string() == "two words"
        && cv_gate_bool.type() == engine::CvarType::Bool
        && cv_gate_int.type() == engine::CvarType::Int
        && cv_gate_float.type() == engine::CvarType::Float
        && cv_gate_string.type() == engine::CvarType::String;

    // Comments, both separators, blank lines, one unknown key, one bad value.
    static constexpr const char* kText =
        "# hash comment\n"
        "// slash comment\n"
        "\n"
        "   \n"
        "gate.text_int 7\n"
        "gate.text_float = 1.5\n"
        "gate.text_string   hello world  \r\n"
        "gate.nothere 1\n"
        "gate.text_int oops\n";
    const auto text_stats = engine::apply_cvar_text(kText, CvarSource::File);

    // "key=value" with no spaces at all: the comment above split_line() in
    // cvar.cpp claims all three separator forms behave identically, but until
    // now only "key value" and "key = value" were exercised.
    const auto text_eq_stats = engine::apply_cvar_text("gate.text_eq=42\n", CvarSource::File);
    const bool text_eq_ok = text_eq_stats.applied == 1 && cv_text_eq.as_int() == 42;

    // The entire reason the precedence rule exists: a File write must lose to
    // a CommandLine-owned value even when it arrives as config text rather
    // than a direct set() call. Until now `ignored` was only ever produced by
    // calling set() directly, never through apply_cvar_text.
    cv_text_prec.set("5", CvarSource::CommandLine);
    const auto text_prec_stats = engine::apply_cvar_text("gate.text_prec 9\n", CvarSource::File);
    const bool text_prec_ok = text_prec_stats.applied == 0
        && text_prec_stats.ignored == 1
        && cv_text_prec.as_int() == 5;

    // Pin the trailing-comment fix: a String knob must have the comment
    // stripped, not stored verbatim.
    const auto text_comment_stats = engine::apply_cvar_text(
        "gate.text_comment borderless   # my note\n", CvarSource::File);
    const bool text_comment_ok = text_comment_stats.applied == 1
        && cv_text_comment.as_string() == "borderless";

    const auto text_empty_stats = engine::apply_cvar_text("", CvarSource::File);
    const bool text_empty_ok = text_empty_stats.applied == 0
        && text_empty_stats.unknown == 0
        && text_empty_stats.invalid == 0
        && text_empty_stats.ignored == 0;

    // A bare key with no separator at all is malformed, not silently ignored.
    const auto text_bare_stats = engine::apply_cvar_text("just_a_bare_key\n", CvarSource::File);
    const bool text_bare_ok = text_bare_stats.invalid == 1 && text_bare_stats.applied == 0;

    const bool text_ok = text_stats.applied == 3
        && text_stats.unknown == 1
        && text_stats.invalid == 1
        && text_stats.ignored == 0
        && cv_text_int.as_int() == 7
        && cv_text_float.as_float() > 1.49f && cv_text_float.as_float() < 1.51f
        && cv_text_string.as_string() == "hello world"
        && text_eq_ok
        && text_prec_ok
        && text_comment_ok
        && text_empty_ok
        && text_bare_ok;

    // A lower source never overwrites a higher one. This is what lets the
    // engine load config.cfg after the command line.
    const bool precedence_ok =
        cv_gate_prec.source() == CvarSource::Default
        && cv_gate_prec.set("1", CvarSource::File) == CvarSetResult::Applied
        && cv_gate_prec.set("2", CvarSource::CommandLine) == CvarSetResult::Applied
        && cv_gate_prec.set("3", CvarSource::File) == CvarSetResult::Ignored
        && cv_gate_prec.as_int() == 2
        && cv_gate_prec.source() == CvarSource::CommandLine
        && cv_gate_prec.set("4", CvarSource::Code) == CvarSetResult::Applied
        && cv_gate_prec.as_int() == 4;

    // Both value forms, plus the two malformed tails.
    static const char* kArgvOk[] = {
        "sandbox.exe", "--gates", "--set", "gate.args=11", "--set", "gate.args", "12"};
    const auto args_stats = engine::apply_cvar_args(7, kArgvOk);
    // Captured immediately: the later dangling/no_value calls below mutate
    // gate.args again, so reading cv_gate_args.as_int() inside the args_ok
    // expression at that point would see the final value (13), not this one.
    const engine::i32 args_after_ok = cv_gate_args.as_int();
    static const char* kArgvDangling[] = {"sandbox.exe", "--set"};
    const auto dangling_stats = engine::apply_cvar_args(2, kArgvDangling);
    static const char* kArgvNoValue[] = {
        "sandbox.exe", "--set", "gate.args", "--set", "gate.args=13"};
    const auto no_value_stats = engine::apply_cvar_args(5, kArgvNoValue);
    const bool args_ok = args_stats.applied == 2
        && args_stats.unknown == 0
        && args_stats.invalid == 0
        && args_after_ok == 12
        && dangling_stats.invalid == 1 && dangling_stats.applied == 0
        && no_value_stats.invalid == 1 && no_value_stats.applied == 1
        && cv_gate_args.as_int() == 13;

    // A Cvar with automatic (non-static) storage duration must leave no trace
    // once it goes out of scope: no dangling pointer in the registry, and no
    // false "duplicate cvar name" abort if the same name is declared again.
    bool scope_ok;
    {
        const engine::usize before = engine::cvar_count();
        bool ok = true;
        {
            engine::Cvar scoped("gate.scoped", false, "Cvar gate: scope-lifetime knob");
            ok = ok
                && engine::find_cvar("gate.scoped") == &scoped
                && engine::cvar_count() == before + 1;
        }
        ok = ok
            && engine::cvar_count() == before
            && engine::find_cvar("gate.scoped") == nullptr;
        {
            // Re-entering an identical scope must not abort: that is the
            // regression the destructor's unregister step exists to prevent.
            engine::Cvar scoped_again("gate.scoped", false, "Cvar gate: scope-lifetime knob");
            ok = ok && engine::find_cvar("gate.scoped") == &scoped_again;
        }
        ok = ok
            && engine::cvar_count() == before
            && engine::find_cvar("gate.scoped") == nullptr;
        scope_ok = ok;
    }

    // The engine's own loader, not a private copy of it.
    bool file_ok = false;
    bool missing_ok = false;
    if (fs) {
        const std::string path = scratch_dir + "/gate_cvars.cfg";
        static constexpr const char* kBody = "# written by the cvar gate\ngate.file 99\n";
        const std::span<const engine::u8> body{
            reinterpret_cast<const engine::u8*>(kBody), std::strlen(kBody)};
        if (fs->write_file(path, body)) {
            bool found = false;
            const auto stats = engine::apply_cvar_file(*fs, path, &found);
            file_ok = found && stats.applied == 1 && stats.invalid == 0
                && cv_gate_file.as_int() == 99;
        }
        bool missing_found = true;
        const auto missing_stats =
            engine::apply_cvar_file(*fs, scratch_dir + "/no_such_file.cfg", &missing_found);
        missing_ok = !missing_found && missing_stats.applied == 0
            && missing_stats.invalid == 0 && missing_stats.unknown == 0;
    }

    const bool passed = registry_ok && text_ok && types_ok && precedence_ok && scope_ok && args_ok
        && file_ok && missing_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Cvar gate: count=%llu registry=%s text=%s types=%s precedence=%s scope=%s args=%s "
        "file=%s missing=%s (%s)",
        static_cast<unsigned long long>(count),
        registry_ok ? "yes" : "no",
        text_ok ? "yes" : "no",
        types_ok ? "yes" : "no",
        precedence_ok ? "yes" : "no",
        scope_ok ? "yes" : "no",
        args_ok ? "yes" : "no",
        file_ok ? "yes" : "no",
        missing_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_identity_gate(engine::Engine& app) {
#ifdef ENGINE_GAME_APP
    const char* expect_title = "Sol";
    const bool expect_icon = true;
#else
    const char* expect_title = "Engine Sandbox";
    const bool expect_icon = false;
#endif
    const bool title_ok = app.window() != nullptr && app.window()->title() == expect_title;
    const bool icon_ok = app.executable_has_icon() == expect_icon;
    const bool version_ok = app.executable_file_version() == ENGINE_APP_FILE_VERSION
        && ENGINE_APP_FILE_VERSION[0] != '\0';

    const bool passed = title_ok && icon_ok && version_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Identity gate: title=%s icon=%s version=%s (%s)",
        expect_title, expect_icon ? "yes" : "no", ENGINE_APP_FILE_VERSION,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_ship_gate(engine::Engine& app) {
    const std::filesystem::path exe_dir{app.executable_directory()};
    std::error_code ec;
    const bool dxc_ok = std::filesystem::exists(exe_dir / "dxcompiler.dll", ec);
    ec.clear();
    const bool dxil_ok = std::filesystem::exists(exe_dir / "dxil.dll", ec);
    ec.clear();
    const bool agility_present =
        std::filesystem::exists(exe_dir / "D3D12Core.dll", ec)
        || std::filesystem::exists(exe_dir / "D3D12" / "D3D12Core.dll", ec);
    const bool agility_ok = !agility_present;

    engine::rhi::IDevice* device = app.device();
    const engine::rhi::GpuBaseline baseline =
        device != nullptr ? device->gpu_baseline() : engine::rhi::GpuBaseline{};
    const bool sm_ok = baseline.shader_model >= engine::rhi::kGpuShaderModel_6_0;
    // A D3D feature level is a D3D fact. GpuBaseline is D3D-shaped and the
    // Vulkan backend reports 0 deliberately rather than inventing a number, so
    // asserting 11_0 unconditionally failed on a device that was working
    // perfectly. Asserted where it means something and reported n/a where it
    // does not - not quietly dropped, because a gate that stops checking
    // without saying so is worse than one that fails.
    const bool d3d = device != nullptr && device->api() == engine::rhi::GraphicsAPI::D3D12;
    const bool fl_ok = !d3d || baseline.feature_level >= engine::rhi::kGpuFeatureLevel_11_0;

    // dxil.dll and dxcompiler.dll are checked on both: they are facts about
    // what this build put next to the exe, not about the backend in play, and
    // the same binary can run either. What a Vulkan-only player build would
    // need instead - cooked SPIR-V, or a SPIR-V-capable dxcompiler.dll, since
    // the SDK's is not redistributed - is unsettled and recorded in
    // docs/GPU_BASELINE.md rather than asserted here.
    const bool passed = dxc_ok && dxil_ok && agility_ok && sm_ok && fl_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Ship gate: api=%s dxc=%s dxil=%s agility=%s sm=%s fl=%s (%s)",
        d3d ? "d3d12" : "vulkan",
        dxc_ok ? "yes" : "no",
        dxil_ok ? "yes" : "no",
        agility_ok ? "os" : "sdk",
        sm_ok ? "6.0" : "no",
        d3d ? (fl_ok ? "11_0" : "no") : "n/a",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

bool run_build_gate(engine::ContentLayout layout) {
#ifdef ENGINE_GAME_APP
    const bool ok = layout == engine::ContentLayout::Install;
    const char* target = "game";
#else
    const bool ok = layout == engine::ContentLayout::Repo
        || layout == engine::ContentLayout::Install;
    const char* target = "sandbox";
#endif
    char message[192];
    std::snprintf(message, sizeof(message),
        "Build gate: target=%s layout=%s (%s)",
        target, engine::content_layout_name(layout), ok ? "pass" : "FAIL");
    engine::log(ok ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return ok;
}

// Exposure: one linear multiplier applied to scene_color at each of its three
// first-read sites, and never to bloom's output.
//
// No GPU readback needed - every assertion is on a value:
//
//   1. EV conversion and its clamp.
//   2. The multiplier survives the renderer's plumbing end to end. This is the
//      load-bearing one: the 29 Aug audit's finding A1 is that a field added to
//      three of the four plumbing structs produces a silently disabled feature,
//      and nothing catches it. extract_visible is where that would happen.
//   3. The two AA paths agree. They composite bloom in different shaders, so if
//      their exposure disagrees, F5 changes image brightness.
//   4. Bloom applies it exactly once - first mip only.
//   5. The cvar round-trips.
bool run_exposure_gate() {
    const bool ev_ok = std::fabs(exposure_from_ev(0.f) - 1.f) < 1.e-6f
        && std::fabs(exposure_from_ev(-1.f) - 0.5f) < 1.e-6f
        && std::fabs(exposure_from_ev(1.f) - 2.f) < 1.e-6f;
    // Out-of-range EV must clamp to something finite rather than inf.
    const engine::f32 huge = exposure_from_ev(1000.f);
    const engine::f32 tiny = exposure_from_ev(-1000.f);
    const bool clamp_ok = std::isfinite(huge) && huge > 0.f && std::isfinite(tiny) && tiny > 0.f;

    // 2: through ExtractDesc -> extract_visible -> RenderSnapshot.
    constexpr engine::f32 kProbe = 0.375f;
    engine::renderer::ExtractDesc desc{};
    desc.exposure = kProbe;
    desc.width = 64;
    desc.height = 64;
    desc.projection = engine::math::Mat4::perspective(1.f, 1.f, 0.1f, 10.f);
    desc.view = engine::math::Mat4::identity();
    engine::Arena arena(64 * 1024);
    engine::renderer::RenderSnapshot snapshot{};
    engine::renderer::extract_visible(desc, arena, snapshot, nullptr);
    const bool plumbed = std::fabs(snapshot.exposure - kProbe) < 1.e-6f;

    // 3: the two composite sites must carry the same number.
    const engine::renderer::tonemap::Constants tm =
        engine::renderer::tonemap::make_constants(kProbe);
    const engine::renderer::taa::Constants ta =
        engine::renderer::taa::make_constants(64, 64, {}, false, kProbe);
    const bool paths_agree = std::fabs(tm.params.x - ta.params.w) < 1.e-6f
        && std::fabs(tm.params.x - kProbe) < 1.e-6f;
    // The tonemap CBV also carries bloom intensity, which used to be duplicated
    // as a literal in tonemap.hlsl.
    const bool intensity_ok =
        std::fabs(tm.params.y - engine::renderer::bloom::kIntensity) < 1.e-6f;

    // 4: first mip exposes, later mips must not re-expose.
    const engine::renderer::bloom::Constants first =
        engine::renderer::bloom::make_downsample_constants(64, 64, true, kProbe);
    const engine::renderer::bloom::Constants later =
        engine::renderer::bloom::make_downsample_constants(32, 32, false, kProbe);
    const bool bloom_once = std::fabs(first.params.y - kProbe) < 1.e-6f
        && std::fabs(later.params.y - 1.f) < 1.e-6f;

    // 5: the cvar exists, is a float, and reads back.
    const engine::Cvar* knob = engine::find_cvar("r.exposure");
    const bool cvar_ok = knob != nullptr && knob->type() == engine::CvarType::Float;

    const bool passed = ev_ok && clamp_ok && plumbed && paths_agree && intensity_ok
        && bloom_once && cvar_ok;
    char message[256];
    std::snprintf(message, sizeof(message),
        "Exposure gate: ev(0,-1,1)=%s clamp=%s plumbed=%.3f/%.3f paths_agree=%s "
        "intensity=%.3f bloom_first=%.3f bloom_later=%.3f cvar=%s (%s)",
        ev_ok ? "yes" : "no", clamp_ok ? "yes" : "no",
        static_cast<double>(snapshot.exposure), static_cast<double>(kProbe),
        paths_agree ? "yes" : "no", static_cast<double>(tm.params.y),
        static_cast<double>(first.params.y), static_cast<double>(later.params.y),
        cvar_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_quality_preset_gate() {
    using Mode = engine::renderer::aa::Mode;
    QualitySettings q{};

    const bool low_ok = resolve_quality("low", nullptr, nullptr, q)
        && q.aa == Mode::Off && q.shadow_size == 512;
    const engine::u32 low_shadow = q.shadow_size;

    const bool medium_ok = resolve_quality("medium", nullptr, nullptr, q)
        && q.aa == Mode::Fxaa && q.shadow_size == 1024;
    const engine::u32 medium_shadow = q.shadow_size;

    const bool high_ok = resolve_quality("high", nullptr, nullptr, q)
        && q.aa == Mode::Taa && q.shadow_size == 2048;
    const engine::u32 high_shadow = q.shadow_size;

    // "custom" is a name, and it must leave the per-knob defaults alone -
    // otherwise adding this row silently changes what an existing config does.
    const bool custom_ok = resolve_quality("custom", nullptr, nullptr, q)
        && q.aa == Mode::Off && q.shadow_size == engine::renderer::kShadowMapSize;

    // An unrecognised name is reported, not silently treated as "custom".
    const bool unknown_ok = !resolve_quality("ultra", nullptr, nullptr, q);

    // A preset is a default, not an override.
    const Mode want_aa = Mode::Fxaa;
    const bool aa_wins = resolve_quality("high", &want_aa, nullptr, q)
        && q.aa == Mode::Fxaa && q.shadow_size == 2048;
    const engine::u32 want_shadow = 4096;
    const bool shadow_wins = resolve_quality("low", nullptr, &want_shadow, q)
        && q.aa == Mode::Off && q.shadow_size == 4096;

    const bool bounds_ok = valid_shadow_size(256) && valid_shadow_size(1024)
        && valid_shadow_size(4096) && !valid_shadow_size(0) && !valid_shadow_size(128)
        && !valid_shadow_size(1000) && !valid_shadow_size(8192);

    const bool passed = low_ok && medium_ok && high_ok && custom_ok && unknown_ok
        && aa_wins && shadow_wins && bounds_ok;

    char message[224];
    std::snprintf(message, sizeof(message),
        "Quality preset gate: low=%s/%u medium=%s/%u high=%s/%u custom=%s unknown=%s "
        "explicit_aa_wins=%s explicit_shadow_wins=%s bounds=%s (%s)",
        low_ok ? "off" : "BAD", low_shadow, medium_ok ? "fxaa" : "BAD", medium_shadow,
        high_ok ? "taa" : "BAD", high_shadow, custom_ok ? "yes" : "no",
        unknown_ok ? "rejected" : "ACCEPTED", aa_wins ? "yes" : "no",
        shadow_wins ? "yes" : "no", bounds_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

// A struct with a field deliberately left out of its table, which is the
// failure `validate` exists to catch. `visible` is absent below.
static constexpr engine::reflect::FieldDesc kMissingTrailingFields[] = {
    ENGINE_REFLECT_FIELD(ReflectProbe, pos, engine::reflect::FieldType::Vec3),
    ENGINE_REFLECT_FIELD(ReflectProbe, radius, engine::reflect::FieldType::F32),
    ENGINE_REFLECT_FIELD(ReflectProbe, flags, engine::reflect::FieldType::U32),
};

// `radius` absent, leaving a four-byte hole in the middle.
static constexpr engine::reflect::FieldDesc kMissingInteriorFields[] = {
    ENGINE_REFLECT_FIELD(ReflectProbe, pos, engine::reflect::FieldType::Vec3),
    ENGINE_REFLECT_FIELD(ReflectProbe, flags, engine::reflect::FieldType::U32),
    ENGINE_REFLECT_FIELD(ReflectProbe, visible, engine::reflect::FieldType::Bool),
};

// `pos` absent, leaving a twelve-byte hole before the first described field.
static constexpr engine::reflect::FieldDesc kMissingLeadingFields[] = {
    ENGINE_REFLECT_FIELD(ReflectProbe, radius, engine::reflect::FieldType::F32),
    ENGINE_REFLECT_FIELD(ReflectProbe, flags, engine::reflect::FieldType::U32),
    ENGINE_REFLECT_FIELD(ReflectProbe, visible, engine::reflect::FieldType::Bool),
};

// A descriptor built through ENGINE_REFLECT_FIELD must agree with the struct on
// every field's name, offset and width. Hand-writing these three is the
// error the macro exists to remove, so this is what proves the macro.
static constexpr engine::reflect::FieldDesc kProbeFields[] = {
    ENGINE_REFLECT_FIELD(ReflectProbe, pos, engine::reflect::FieldType::Vec3),
    ENGINE_REFLECT_FIELD(ReflectProbe, radius, engine::reflect::FieldType::F32),
    ENGINE_REFLECT_FIELD(ReflectProbe, flags, engine::reflect::FieldType::U32),
    ENGINE_REFLECT_FIELD(ReflectProbe, visible, engine::reflect::FieldType::Bool),
};
static constexpr engine::reflect::TypeDesc kProbeType{
    "ReflectProbe", sizeof(ReflectProbe), alignof(ReflectProbe), kProbeFields};

// The usability claim, enforced by the compiler rather than described: a type
// checks its own descriptors where they are defined, and a forgotten field is
// a build error rather than a runtime discovery. This line is the reason
// validate is constexpr.
static_assert(engine::reflect::validate(kProbeType) == engine::reflect::TypeError::Ok,
    "ReflectProbe's descriptors do not match the struct");

// And the negative: the table that omits `visible` must not compile clean.
static_assert(engine::reflect::validate(
                  engine::reflect::TypeDesc{"P", sizeof(ReflectProbe),
                      alignof(ReflectProbe), kMissingTrailingFields})
        == engine::reflect::TypeError::TrailingGapTooLarge,
    "validate must reject a table missing a trailing field");

bool run_reflect_gate() {
    using engine::reflect::FieldType;
    using engine::reflect::size_of;
    using engine::reflect::align_of;

    // Every byte count reflect hardcodes, against the type it claims to
    // describe. reflect cannot include math (it is Layer 0 and math is a
    // sibling), so this gate is the only place the two can be compared - and
    // a silent disagreement here would corrupt every offset computed from it.
    struct Expect {
        FieldType type;
        engine::u32 size;
        engine::u32 align;
    };
    const Expect expected[] = {
        {FieldType::Bool, sizeof(bool), alignof(bool)},
        {FieldType::I32, sizeof(engine::i32), alignof(engine::i32)},
        {FieldType::U32, sizeof(engine::u32), alignof(engine::u32)},
        {FieldType::F32, sizeof(engine::f32), alignof(engine::f32)},
        {FieldType::F64, sizeof(engine::f64), alignof(engine::f64)},
        {FieldType::Vec2, sizeof(engine::math::Vec2), alignof(engine::math::Vec2)},
        {FieldType::Vec3, sizeof(engine::math::Vec3), alignof(engine::math::Vec3)},
        {FieldType::Vec4, sizeof(engine::math::Vec4), alignof(engine::math::Vec4)},
        {FieldType::Mat4, sizeof(engine::math::Mat4), alignof(engine::math::Mat4)},
    };

    engine::u32 checked = 0;
    engine::u32 wrong = 0;
    FieldType first_wrong = FieldType::Bool;
    engine::u32 first_got = 0;
    engine::u32 first_want = 0;
    for (const Expect& e : expected) {
        ++checked;
        const engine::u32 got_size = size_of(e.type);
        const engine::u32 got_align = align_of(e.type);
        if (got_size != e.size || got_align != e.align) {
            if (wrong == 0) {
                first_wrong = e.type;
                first_got = got_size;
                first_want = e.size;
            }
            ++wrong;
        }
    }

    // Name is per-field, so it must report kVariableSize rather than a width.
    const bool name_variable = size_of(FieldType::Name) == engine::reflect::kVariableSize;

    // `expected[]` is hand-written because only the sandbox can name the real
    // C++ types, so nothing can derive it - but the *count* can be checked.
    // Name is verified separately just above, hence the + 1. A new FieldType
    // with no entry here turns this red instead of passing while blind to it.
    const engine::u32 type_count = static_cast<engine::u32>(FieldType::Count);
    const bool covered = checked + 1 == type_count;

    const bool desc_ok = kProbeFields[0].offset == offsetof(ReflectProbe, pos)
        && kProbeFields[1].offset == offsetof(ReflectProbe, radius)
        && kProbeFields[2].offset == offsetof(ReflectProbe, flags)
        && kProbeFields[3].offset == offsetof(ReflectProbe, visible)
        && std::string_view(kProbeFields[0].name) == "pos"
        && std::string_view(kProbeFields[1].name) == "radius"
        && std::string_view(kProbeFields[2].name) == "flags"
        && std::string_view(kProbeFields[3].name) == "visible"
        && kProbeFields[0].size == sizeof(ReflectProbe::pos)
        && kProbeFields[3].size == sizeof(ReflectProbe::visible);

    using engine::reflect::TypeError;
    using engine::reflect::validate;
    using engine::reflect::TypeDesc;

    // A wrong FieldType for a field whose width is known.
    static constexpr engine::reflect::FieldDesc kWrongTypeFields[] = {
        {"pos", 0, 12, FieldType::F32},
    };
    // Two fields at the same offset.
    static constexpr engine::reflect::FieldDesc kOverlapFields[] = {
        {"a", 0, 4, FieldType::U32},
        {"b", 2, 4, FieldType::U32},
    };
    // Descending offsets. Three fields, not two: the first two must be
    // contiguous from offset 0 so that the leading-gap rule does not fire
    // first and mask what this case is for.
    static constexpr engine::reflect::FieldDesc kUnorderedFields[] = {
        {"a", 0, 4, FieldType::U32},
        {"b", 4, 4, FieldType::U32},
        {"c", 0, 4, FieldType::U32},
    };
    // A field running past the end of the struct.
    static constexpr engine::reflect::FieldDesc kPastEndFields[] = {
        {"a", 0, 4, FieldType::U32},
    };
    // The sentinel named as a field's type. size_of reports kVariableSize for
    // it, so without validate's explicit guard this would pass as though the
    // width were per-field.
    static constexpr engine::reflect::FieldDesc kSentinelFields[] = {
        {"a", 0, 4, FieldType::Count},
    };

    struct Case {
        const char* label;
        TypeDesc type;
        TypeError want;
    };
    const Case cases[] = {
        {"good", kProbeType, TypeError::Ok},
        {"missing_leading",
            TypeDesc{"P", sizeof(ReflectProbe), alignof(ReflectProbe),
                kMissingLeadingFields},
            TypeError::GapTooLarge},
        {"missing_trailing",
            TypeDesc{"P", sizeof(ReflectProbe), alignof(ReflectProbe),
                kMissingTrailingFields},
            TypeError::TrailingGapTooLarge},
        {"missing_interior",
            TypeDesc{"P", sizeof(ReflectProbe), alignof(ReflectProbe),
                kMissingInteriorFields},
            TypeError::GapTooLarge},
        {"wrong_type", TypeDesc{"P", 12, 4, kWrongTypeFields},
            TypeError::SizeDisagreesWithType},
        {"overlap", TypeDesc{"P", 8, 4, kOverlapFields},
            TypeError::FieldOverlapsPrevious},
        {"unordered", TypeDesc{"P", 12, 4, kUnorderedFields},
            TypeError::OffsetsNotAscending},
        {"past_end", TypeDesc{"P", 2, 4, kPastEndFields},
            TypeError::FieldPastEnd},
        {"no_fields", TypeDesc{"P", 4, 4, {}}, TypeError::NoFields},
        {"sentinel_type", TypeDesc{"P", 4, 4, kSentinelFields},
            TypeError::InvalidFieldType},
    };

    engine::u32 cases_ok = 0;
    bool have_bad = false;
    const char* first_bad_case = "-";
    TypeError first_bad_got = TypeError::Ok;
    for (const Case& c : cases) {
        const TypeError got = validate(c.type);
        if (got == c.want) {
            ++cases_ok;
        } else if (!have_bad) {
            have_bad = true;
            first_bad_case = c.label;
            first_bad_got = got;
        }
    }
    const engine::u32 case_count = static_cast<engine::u32>(std::size(cases));

    const bool passed = wrong == 0 && checked == 9 && name_variable && covered
        && desc_ok && cases_ok == case_count;
    char message[256];
    std::snprintf(message, sizeof(message),
        "Reflect gate: sizes=%u/%u covered=%u/%u name_var=%s desc_ok=%s "
        "validate=%u/%u first_bad=%s(got %s) (%s)",
        checked - wrong, checked, checked + 1, type_count,
        name_variable ? "yes" : "no", desc_ok ? "yes" : "no",
        cases_ok, case_count, first_bad_case,
        engine::reflect::to_string(first_bad_got), passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
}

} // namespace sandbox
