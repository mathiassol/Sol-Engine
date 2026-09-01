// `--gates-cpu`: every gate that needs no GPU, run with no platform backend.
//
// The Linux CI job compiles this tree and then stops, because run_app needs a
// platform and an RHI and neither exists there. So nothing in this repository
// has ever *executed* off Windows, and a sanitizer job over code that never
// runs finds nothing. This is the entry point that changes that: 34 of the 72
// gates are classified Cpu in kGates, and they exercise core, math, scene,
// physics-cpu, assets, the renderer's CPU maths and gameplay.
//
// The filesystem here is a local std::filesystem implementation rather than
// platform-win32's. It lives in the sandbox on purpose - it is test scaffolding
// so gates can run, not an engine capability, and a platform-std package for
// one class would be a package scaffolded for its own sake.

#include "gates/gate_registry.hpp"

#include <engine/assets/filesystem/asset_loader_filesystem.hpp>
#include <engine/core/log.hpp>
#include <engine/platform/filesystem.hpp>

#ifdef ENGINE_HAS_PHYSICS_CPU
#include <engine/physics/cpu/physics_cpu.hpp>
#endif

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace sandbox {
namespace {

class StdFileSystem final : public engine::platform::IFileSystem {
public:
    bool read_file(std::string_view path, std::vector<engine::u8>& out) override {
        std::ifstream in(std::filesystem::path(path), std::ios::binary | std::ios::ate);
        if (!in) {
            return false;
        }
        const std::streamsize size = in.tellg();
        if (size < 0) {
            return false;
        }
        in.seekg(0);
        out.resize(static_cast<engine::usize>(size));
        if (out.empty()) {
            return true;
        }
        return static_cast<bool>(in.read(reinterpret_cast<char*>(out.data()), size));
    }

    bool write_file(std::string_view path, std::span<const engine::u8> data) override {
        const std::filesystem::path target(path);
        std::error_code ec;
        if (target.has_parent_path()) {
            std::filesystem::create_directories(target.parent_path(), ec);
        }
        std::ofstream out(target, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        if (data.empty()) {
            return true;
        }
        return static_cast<bool>(
            out.write(reinterpret_cast<const char*>(data.data()),
                static_cast<std::streamsize>(data.size())));
    }

    bool exists(std::string_view path) const override {
        std::error_code ec;
        return std::filesystem::exists(std::filesystem::path(path), ec);
    }

    std::string resolve(std::string_view path) const override {
        std::error_code ec;
        const auto abs = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
        if (ec) {
            return std::string(path);
        }
        return abs.generic_string();
    }
};

// argv[0] rather than a platform call: this runs where there is no platform
// backend, and CI invokes the binary by path. Falls back to the working
// directory, which is what an invocation through PATH would leave us with.
std::filesystem::path executable_directory(const char* argv0) {
    std::error_code ec;
    if (argv0 != nullptr && *argv0 != '\0') {
        const auto full = std::filesystem::weakly_canonical(std::filesystem::path(argv0), ec);
        if (!ec && full.has_parent_path()) {
            return full.parent_path();
        }
    }
    const auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path(".") : cwd;
}

} // namespace

int run_headless_gates(const char* argv0) {
    const std::filesystem::path exe_dir = executable_directory(argv0);

    StdFileSystem fs;
    auto loader = engine::assets::filesystem::create_asset_loader(fs);

    // Same mount names the engine uses, so the gates that resolve
    // "/content/..." and "/debug/..." see what they see in a real run.
    bool mounted = true;
    if (loader) {
        mounted = loader->mount("content", (exe_dir / "content").generic_string())
            && loader->mount("debug", (exe_dir / "debug").generic_string());
    }
    if (!mounted) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Assets,
            "Headless: content mount failed - gates that read content will report it");
    }

    std::unique_ptr<engine::physics::IPhysics> physics;
#ifdef ENGINE_HAS_PHYSICS_CPU
    physics = engine::physics::cpu::create_physics();
#endif

    CpuGateContext ctx{};
    ctx.fs = &fs;
    ctx.loader = loader.get();
    ctx.physics = physics.get();
    ctx.layout = engine::ContentLayout::Install;
    ctx.scratch_dir = exe_dir.generic_string();

    char banner[256];
    std::snprintf(banner, sizeof(banner), "Headless gates starting in %s",
        exe_dir.generic_string().c_str());
    engine::log(engine::LogLevel::Info, engine::LogChannel::General, banner);

    return run_cpu_gates(ctx) ? 0 : 1;
}

} // namespace sandbox
