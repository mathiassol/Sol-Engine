#include "../sandbox_common.hpp"

#include <engine/core/crash.hpp>

#include <filesystem>
#include <fstream>
#include <system_error>

#ifdef ENGINE_HAS_WIN32_PLATFORM
#include <engine/platform/win32/crash_win32.hpp>
#endif

// Platform and input gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

bool run_minidump_gate() {
#ifdef ENGINE_HAS_WIN32_PLATFORM
    // MiniDumpWriteDump can capture a *running* process, so this drives the
    // writer directly against a temp directory rather than staging a real
    // crash - the same shape run_file_log_gate uses for create_file_logger.
    const auto dir = std::filesystem::temp_directory_path() / "sol-minidump-gate";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    const auto path = dir / "probe.dmp";

    const bool wrote = engine::platform::win32::write_minidump(path.generic_string());
    const bool exists = std::filesystem::exists(path, ec);
    const engine::u64 size = exists
        ? static_cast<engine::u64>(std::filesystem::file_size(path, ec)) : 0u;

    // "MDMP" is the minidump signature. Asserting the magic and not just a
    // non-zero size is the difference between "a file appeared" and "a debugger
    // can open it".
    char magic[5] = {};
    if (exists) {
        std::ifstream in(path, std::ios::binary);
        in.read(magic, 4);
    }
    const bool magic_ok = magic[0] == 'M' && magic[1] == 'D' && magic[2] == 'M' && magic[3] == 'P';
    // A dump with no stack in it is not worth attaching to a bug report.
    const bool sized_ok = size > 4096u;

    // The hook core calls on a failed assert must be installed, or 76 live
    // assert sites produce nothing. Checked by installing into a scratch
    // directory and confirming write_crash_dump lands a file there.
    const auto hook_dir = dir / "hook";
    const std::string installed
        = engine::platform::win32::install_crash_dumps(hook_dir.generic_string());
    engine::write_crash_dump("gate");
    const bool hook_ok = !installed.empty()
        && std::filesystem::exists(hook_dir / "crash-gate.dmp", ec);
    // Leave nothing installed: this is a gate run, and Foundation #6 made the
    // same call for the file logger.
    engine::set_crash_dump_handler(nullptr);
    std::filesystem::remove_all(dir, ec);

    const bool passed = wrote && exists && magic_ok && sized_ok && hook_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Minidump gate: wrote=%s magic=%s bytes=%llu assert_hook=%s (%s)",
        wrote ? "yes" : "no", magic_ok ? "MDMP" : "BAD",
        static_cast<unsigned long long>(size), hook_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::General, message);
    return passed;
#else
    // Minidumps are a Windows API. The gate exists on every platform so the
    // registry stays uniform; there is nothing to assert off Windows.
    engine::log(engine::LogLevel::Info, engine::LogChannel::General,
        "Minidump gate: not applicable off Windows (pass)");
    return true;
#endif
}


bool run_window_gate(engine::platform::IWindow* window, engine::rhi::IDevice* device) {
    using engine::platform::WindowMode;

    if (!window) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Platform,
            "Window gate: no window (FAIL)");
        return false;
    }

    // window.mode and r.vsync are now user-settable, so asserting the factory
    // default unconditionally would go red for any developer with a config.cfg.
    // Assert the factory default only while a knob is untouched; once a knob has
    // set it, assert the window matches what the knob asked for instead. That
    // tests the wiring, which is the assertion worth having. The knobs live in
    // engine.cpp's anonymous namespace -- the public registry is how a gate
    // reaches them without exporting engine internals.
    const engine::Cvar* cv_mode = engine::find_cvar("window.mode");
    const engine::Cvar* cv_vsync_knob = engine::find_cvar("r.vsync");

    bool mode_ok = window->mode() == WindowMode::Windowed;
    if (cv_mode && cv_mode->type() == engine::CvarType::String
        && cv_mode->source() != engine::CvarSource::Default) {
        WindowMode wanted = WindowMode::Windowed;
        // An unparsable value is refused by the engine, which keeps the default.
        if (!engine::platform::parse_window_mode(cv_mode->as_string(), wanted)) {
            wanted = WindowMode::Windowed;
        }
        mode_ok = window->mode() == wanted;
    }

    bool vsync_startup_ok = window->vsync();
    if (cv_vsync_knob && cv_vsync_knob->type() == engine::CvarType::Bool
        && cv_vsync_knob->source() != engine::CvarSource::Default) {
        vsync_startup_ok = window->vsync() == cv_vsync_knob->as_bool();
    }

    const bool default_ok = mode_ok && vsync_startup_ok;

    // The restore check below compares against the windowed dimensions, so the
    // baseline has to be read while the window really is windowed -- startup is
    // no longer guaranteed to be. Without this, a window.mode knob reds the gate
    // on the restore clause instead of the startup clause.
    const WindowMode startup_mode = window->mode();
    const bool startup_vsync = window->vsync();
    if (startup_mode != WindowMode::Windowed) {
        window->set_mode(WindowMode::Windowed);
    }
    const engine::u32 windowed_w = window->width();
    const engine::u32 windowed_h = window->height();

    const bool borderless_ok = window->set_mode(WindowMode::Borderless)
        && window->mode() == WindowMode::Borderless
        && window->width() > 0 && window->height() > 0;
    const bool fullscreen_ok = window->set_mode(WindowMode::Fullscreen)
        && window->mode() == WindowMode::Fullscreen;
    const bool restore_ok = window->set_mode(WindowMode::Windowed)
        && window->mode() == WindowMode::Windowed
        && window->width() == windowed_w
        && window->height() == windowed_h;

    window->set_vsync(false);
    const bool vsync_off = !window->vsync();
    window->set_vsync(true);
    const bool vsync_on = window->vsync();

    // Hand the window back in the state the user asked for. The sweep above used
    // to leave it windowed with vsync on, which silently undid the knobs a
    // moment after startup applied them.
    if (startup_mode != WindowMode::Windowed) {
        window->set_mode(startup_mode);
    }
    if (!startup_vsync) {
        window->set_vsync(false);
    }

    bool present_ok = device == nullptr;
    if (device) {
        device->set_present_interval(0);
        present_ok = device->present_interval() == 0;
        device->set_present_interval(1);
        present_ok = present_ok && device->present_interval() == 1;
    }

    const bool passed = default_ok && borderless_ok && fullscreen_ok && restore_ok
        && vsync_off && vsync_on && present_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Window gate: startup=%s borderless=%s fullscreen=%s restore=%s vsync=%s present=%s (%s)",
        default_ok ? "yes" : "no",
        borderless_ok ? "yes" : "no",
        fullscreen_ok ? "yes" : "no",
        restore_ok ? "yes" : "no",
        (vsync_off && vsync_on) ? "yes" : "no",
        present_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Platform, message);
    return passed;
}

bool run_audio_gate(engine::audio::IAudio* audio) {
    using engine::audio::parse_wav;
    using engine::audio::WavPcm;
    using engine::audio::write_pcm16_wav;

    const engine::i16 samples[] = {0, 8192, 16384, 8192, 0, -8192, -16384, -8192};
    const auto wav = write_pcm16_wav(samples, 22050, 1);
    WavPcm parsed{};
    const bool wav_ok = parse_wav(wav, parsed) && parsed.sample_rate == 22050
        && parsed.channels == 1 && parsed.bits_per_sample == 16 && parsed.samples.size() == 16;
    const engine::u8 garbage[] = {'N', 'O', 'P', 'E'};
    WavPcm rejected{};
    const bool reject_ok = !parse_wav(garbage, rejected);

    const bool backend_ok = audio != nullptr && audio->name() == "xaudio2";
    bool play_ok = false;
    if (audio) {
        const auto tone = engine::audio::make_tone_pcm16(22050, 80, 440.f, 0.15f);
        engine::audio::SoundDesc desc{};
        desc.pcm = {reinterpret_cast<const engine::u8*>(tone.data()),
            tone.size() * sizeof(engine::i16)};
        desc.sample_rate = 22050;
        desc.channels = 1;
        const auto handle = audio->create_sound(desc);
        play_ok = handle.valid() && audio->play(handle, 0.15f) && audio->playing_count() >= 1;
        audio->tick();
    }

    const bool passed = wav_ok && reject_ok && backend_ok && play_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Audio gate: wav=yes oneshot=yes backend=xaudio2 (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Audio, message);
    return passed;
}

bool run_gamepad_gate(engine::platform::IInput* input) {
    using engine::platform::GamepadButton;
    using engine::platform::GamepadState;
    using engine::platform::apply_stick_deadzone;
    using engine::platform::gamepad_begin_frame;
    using engine::platform::gamepad_set_button;
    using engine::platform::kMaxGamepads;

    engine::f32 zx = 0.05f;
    engine::f32 zy = 0.02f;
    apply_stick_deadzone(zx, zy, 0.2f);
    engine::f32 ox = 0.8f;
    engine::f32 oy = 0.f;
    apply_stick_deadzone(ox, oy, 0.2f);
    const bool deadzone_ok = std::abs(zx) < 1.e-5f && std::abs(zy) < 1.e-5f
        && ox > 0.7f && ox <= 1.001f && std::abs(oy) < 0.01f;

    GamepadState pad{};
    gamepad_begin_frame(pad);
    gamepad_set_button(pad, GamepadButton::A, true);
    const bool press_ok = pad.buttons_pressed[static_cast<engine::usize>(GamepadButton::A)]
        && pad.buttons_down[static_cast<engine::usize>(GamepadButton::A)];
    gamepad_begin_frame(pad);
    gamepad_set_button(pad, GamepadButton::A, true);
    const bool hold_ok = !pad.buttons_pressed[static_cast<engine::usize>(GamepadButton::A)]
        && pad.buttons_down[static_cast<engine::usize>(GamepadButton::A)];
    gamepad_begin_frame(pad);
    gamepad_set_button(pad, GamepadButton::A, false);
    const bool release_ok = pad.buttons_released[static_cast<engine::usize>(GamepadButton::A)]
        && !pad.buttons_down[static_cast<engine::usize>(GamepadButton::A)];
    const bool button_ok = press_ok && hold_ok && release_ok
        && kMaxGamepads == 4
        && static_cast<engine::u32>(GamepadButton::Count) >= 14;

    bool poll_ok = false;
    bool connected = false;
    if (input) {
        const auto& g = input->state().gamepads[0];
        connected = g.connected;
        poll_ok = std::abs(g.left_x) <= 1.f && std::abs(g.left_y) <= 1.f
            && std::abs(g.right_x) <= 1.f && std::abs(g.right_y) <= 1.f
            && g.left_trigger >= 0.f && g.left_trigger <= 1.f
            && g.right_trigger >= 0.f && g.right_trigger <= 1.f;
    }

    const bool passed = deadzone_ok && button_ok && poll_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Gamepad gate: deadzone=%s button=%s poll=%s connected=%s (%s)",
        deadzone_ok ? "yes" : "no", button_ok ? "yes" : "no", poll_ok ? "yes" : "no",
        connected ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Platform, message);
    return passed;
}

} // namespace sandbox
