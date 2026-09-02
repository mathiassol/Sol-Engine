#include "../sandbox_common.hpp"

// RHI, shader compiler and swapchain gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

bool run_storage_texture_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api) {
    // RHI #9. Until this landed, a compute pass could be *ordered* against a
    // graph resource but not write one - no Access mapped to the storage state,
    // so PassKind::Compute carried half a feature.
    engine::shaders::ShaderCompileDesc cs_desc{};
    cs_desc.file_path = shader_path;
    cs_desc.entry_point = "cs_main";
    cs_desc.target_profile = "cs_6_0";
    // From the caller: the same gate runs against both devices, and handing
    // DXIL to a Vulkan device is what this parameter exists to prevent. The
    // first version of the Vulkan call omitted it and vkCreateShaderModule
    // rejected 'DXBC' as a magic number.
    cs_desc.target = target;
    engine::shaders::ShaderBytecode cs{};
    std::string error;
    const bool compiled = compiler.compile(cs_desc, cs, error) && !cs.data.empty();

    engine::rhi::ComputePipelineDesc pipeline{};
    pipeline.compute_shader
        = compiled ? std::span<const engine::u8>(cs.data) : std::span<const engine::u8>{};
    // Two unordered slots: the texture at u0, the readback buffer at u1.
    pipeline.storage_texture_count = 2;
    pipeline.debug_name = "storage_texture_gate";
    auto pso = compiled ? device.create_compute_pipeline(pipeline) : nullptr;

    engine::rhi::TextureDesc tex{};
    tex.width = 8;
    tex.height = 8;
    tex.format = engine::rhi::Format::RGBA8_UNORM;
    tex.usage = engine::rhi::TextureUsage::StorageShaderResource;
    auto storage_tex = device.create_texture(tex, nullptr);
    // The sampled view exists too - that is what StorageShaderResource promises,
    // and what a later graphics pass would bind.
    const bool created = storage_tex != nullptr;

    constexpr engine::usize kBytes = 4 * sizeof(engine::u32);
    engine::rhi::BufferDesc storage{};
    storage.size = kBytes;
    storage.usage = engine::rhi::BufferUsage::Storage;
    auto rw = device.create_buffer(storage);
    engine::rhi::BufferDesc readback{};
    readback.size = kBytes;
    readback.usage = engine::rhi::BufferUsage::Readback;
    auto rb = device.create_buffer(readback);

    engine::u32 probes[4]{};
    bool values_ok = false;
    if (pso && created && rw && rb) {
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.transition(*storage_tex, engine::rhi::ResourceState::Common,
            engine::rhi::ResourceState::Storage);
        cmd.set_compute_pipeline(*pso);
        cmd.set_unordered_access(0, *storage_tex);
        cmd.set_unordered_access(1, *rw);
        cmd.dispatch(1, 1, 1);
        cmd.transition(*rw, engine::rhi::ResourceState::Storage,
            engine::rhi::ResourceState::CopySrc);
        cmd.copy_buffer(*rw, *rb, kBytes);
        cmd.end();
        device.submit();
        device.wait_idle();
        device.read_buffer(*rb, 0, probes, kBytes);

        // The four texels the shader probed, each written by a different thread.
        auto packed = [](engine::u32 x, engine::u32 y) { return x | (y << 8); };
        values_ok = probes[0] == packed(1, 0) && probes[1] == packed(0, 1)
            && probes[2] == packed(7, 3) && probes[3] == packed(3, 7);
    }

    const bool passed = compiled && pso != nullptr && created && values_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Storage texture gate [%s]: created=%s dispatch=%s probes=%u,%u,%u,%u "
        "cross_thread_readback=%s (%s)",
        api, created ? "yes" : "no", pso ? "yes" : "no", probes[0], probes[1], probes[2],
        probes[3],
        values_ok ? "yes" : "NO", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}


bool run_msaa_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler,
    const std::string& shader_path, engine::shaders::ShaderTarget target, const char* api) {
    // RHI #18. Multisampling is the first RHI feature whose shape differs
    // between D3D12 and Vulkan - one resolves with a call after the pass, the
    // other with an attachment on it - so the contract had to say *what*
    // (`RenderPassInfo::resolve`) and let the backend pick *how*. This gate is
    // what makes that claim checkable before a second backend exists.
    //
    // Four assertions, none of which is "it did not crash":
    //   1. a 4x target reports 4 and its resolve target reports 1
    //   2. binding a 1x pipeline inside a 4x pass is diagnosed by name
    //   3. the resolve lands at the destination's single-sample extent
    //   4. the resolved edge has partial-coverage texels and the 1x edge has
    //      none - the actual difference multisampling exists to make

    constexpr engine::u32 kExtent = 64;
    constexpr engine::u32 kSamples = 4;

    auto compile = [&](const char* entry, const char* profile,
                       engine::shaders::ShaderBytecode& out) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = entry;
        desc.target_profile = profile;
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode vs{};
    engine::shaders::ShaderBytecode ps{};
    engine::shaders::ShaderBytecode cs{};
    const bool compiled = compile("vs_main", "vs_6_0", vs) && compile("ps_main", "ps_6_0", ps)
        && compile("cs_count", "cs_6_0", cs);

    engine::rhi::TextureDesc multisampled{};
    multisampled.width = kExtent;
    multisampled.height = kExtent;
    multisampled.format = engine::rhi::Format::RGBA8_UNORM;
    multisampled.usage = engine::rhi::TextureUsage::RenderTarget;
    multisampled.sample_count = kSamples;
    auto ms_target = device.create_texture(multisampled, nullptr);

    engine::rhi::TextureDesc single{};
    single.width = kExtent;
    single.height = kExtent;
    single.format = engine::rhi::Format::RGBA8_UNORM;
    single.usage = engine::rhi::TextureUsage::ColorShaderResource;
    auto resolved = device.create_texture(single, nullptr);
    auto reference = device.create_texture(single, nullptr);

    // 1. The count survives creation and the resolve target stays single.
    const bool counts_reported = ms_target && resolved && reference
        && ms_target->sample_count() == kSamples && resolved->sample_count() == 1
        && reference->sample_count() == 1;

    auto make_pipeline = [&](engine::u32 samples, const char* name) {
        engine::rhi::GraphicsPipelineDesc desc{};
        desc.vertex_shader = std::span<const engine::u8>(vs.data);
        desc.pixel_shader = std::span<const engine::u8>(ps.data);
        desc.color_format = engine::rhi::Format::RGBA8_UNORM;
        desc.depth = engine::rhi::DepthTest::Disabled;
        desc.depth_write = false;
        desc.cull = engine::rhi::CullMode::None;
        desc.sample_count = samples;
        desc.debug_name = name;
        return device.create_graphics_pipeline(desc);
    };
    auto pso_4x = compiled ? make_pipeline(kSamples, "msaa_gate_4x") : nullptr;
    auto pso_1x = compiled ? make_pipeline(1, "msaa_gate_1x") : nullptr;

    constexpr engine::usize kCountBytes = 4 * sizeof(engine::u32);
    auto make_buffer = [&](engine::rhi::BufferUsage usage) {
        engine::rhi::BufferDesc desc{};
        desc.size = kCountBytes;
        desc.usage = usage;
        return device.create_buffer(desc);
    };
    auto resolved_counts = make_buffer(engine::rhi::BufferUsage::Storage);
    auto reference_counts = make_buffer(engine::rhi::BufferUsage::Storage);
    auto resolved_readback = make_buffer(engine::rhi::BufferUsage::Readback);
    auto reference_readback = make_buffer(engine::rhi::BufferUsage::Readback);

    engine::rhi::ComputePipelineDesc count_desc{};
    count_desc.compute_shader
        = compiled ? std::span<const engine::u8>(cs.data) : std::span<const engine::u8>{};
    count_desc.storage_texture_count = 1;  // the count buffer at u0
    count_desc.sampled_texture_count = 1;  // the image at t0
    count_desc.debug_name = "msaa_gate_count";
    auto count_pso = compiled ? device.create_compute_pipeline(count_desc) : nullptr;

    // 2. Swap in a logger that watches for the mismatch diagnostic while the
    // wrong pipeline is deliberately bound. Everything else still reaches the
    // real sink, so a genuine error during the frame is not swallowed.
    class MismatchWatcher final : public engine::ILogger {
    public:
        explicit MismatchWatcher(engine::ILogger* next) : next_(next) {}
        void log(engine::LogLevel level, engine::LogChannel channel,
            std::string_view message) override {
            if (message.find("sample count") != std::string_view::npos) {
                ++hits_;
                return;
            }
            if (next_ != nullptr) {
                next_->log(level, channel, message);
            }
        }
        engine::u32 hits() const { return hits_; }
        engine::ILogger* next() const { return next_; }

    private:
        engine::ILogger* next_ = nullptr;
        engine::u32 hits_ = 0;
    };

    engine::u32 resolved_probe[4]{};
    engine::u32 reference_probe[4]{};
    engine::u32 mismatch_hits = 0;
    const bool ready = counts_reported && pso_4x && pso_1x && count_pso && resolved_counts
        && reference_counts && resolved_readback && reference_readback;

    if (ready) {
        MismatchWatcher watcher(engine::logger());
        engine::set_logger(&watcher);

        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();

        using State = engine::rhi::ResourceState;
        cmd.transition(*ms_target, State::Common, State::RenderTarget);
        cmd.transition(*resolved, State::Common, State::RenderTarget);
        cmd.transition(*reference, State::Common, State::RenderTarget);

        engine::rhi::RenderPassInfo ms_pass{};
        ms_pass.color = ms_target.get();
        ms_pass.resolve = resolved.get();
        ms_pass.clear_color_target = true;
        ms_pass.clear_depth = false;
        cmd.begin_render_pass(ms_pass);
        // The mismatch check, inside the pass and before the real pipeline. No
        // draw follows it, so the runtime never sees an invalid one - the point
        // is the diagnostic, not the failure it would otherwise become.
        cmd.set_pipeline(*pso_1x);
        cmd.set_pipeline(*pso_4x);
        cmd.draw(3, 0);
        cmd.end_render_pass();

        engine::rhi::RenderPassInfo single_pass{};
        single_pass.color = reference.get();
        single_pass.clear_color_target = true;
        single_pass.clear_depth = false;
        cmd.begin_render_pass(single_pass);
        cmd.set_pipeline(*pso_1x);
        cmd.draw(3, 0);
        cmd.end_render_pass();

        cmd.transition(*resolved, State::RenderTarget, State::ShaderRead);
        cmd.transition(*reference, State::RenderTarget, State::ShaderRead);

        const engine::u32 groups = kExtent / 8;
        cmd.set_compute_pipeline(*count_pso);
        cmd.set_unordered_access(0, *resolved_counts);
        cmd.set_shader_resource(0, *resolved);
        cmd.dispatch(groups, groups, 1);
        cmd.set_unordered_access(0, *reference_counts);
        cmd.set_shader_resource(0, *reference);
        cmd.dispatch(groups, groups, 1);

        cmd.transition(*resolved_counts, State::Storage, State::CopySrc);
        cmd.transition(*reference_counts, State::Storage, State::CopySrc);
        cmd.copy_buffer(*resolved_counts, *resolved_readback, kCountBytes);
        cmd.copy_buffer(*reference_counts, *reference_readback, kCountBytes);
        cmd.end();
        device.submit();
        device.wait_idle();
        device.read_buffer(*resolved_readback, 0, resolved_probe, kCountBytes);
        device.read_buffer(*reference_readback, 0, reference_probe, kCountBytes);

        mismatch_hits = watcher.hits();
        engine::set_logger(watcher.next());
    }

    const engine::u32 resolved_blend = resolved_probe[0];
    const engine::u32 resolved_lit = resolved_probe[1];
    const engine::u32 reference_blend = reference_probe[0];
    const engine::u32 reference_lit = reference_probe[1];

    // 3. The resolve wrote the destination at its own extent, so roughly half
    // the 4,096 texels are lit either way - the triangle covers half the target.
    constexpr engine::u32 kHalf = (kExtent * kExtent) / 2;
    const bool extent_ok = resolved_lit > kHalf / 2 && resolved_lit < kHalf * 3 / 2
        && reference_lit > kHalf / 2 && reference_lit < kHalf * 3 / 2;

    // 4. A single-sample raster cannot produce a partial-coverage texel; the
    // resolved one produces roughly one per row the diagonal crosses.
    const bool smoother = reference_blend == 0 && resolved_blend >= kExtent / 2;

    const bool diagnosed = mismatch_hits > 0;
    const bool passed = ready && extent_ok && smoother && diagnosed;

    char message[256];
    std::snprintf(message, sizeof(message),
        "MSAA gate [%s]: samples=%u/%u resolved=(blend=%u lit=%u) single=(blend=%u lit=%u) "
        "mismatch_diagnosed=%u (%s)",
        api, ms_target ? ms_target->sample_count() : 0u, resolved ? resolved->sample_count() : 0u,
        resolved_blend, resolved_lit, reference_blend, reference_lit, mismatch_hits,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

#ifdef ENGINE_HAS_VULKAN
bool run_vulkan_device_gate() {
    // Standing the device up is its own gate, because everything after it is
    // meaningless if the instance, the physical-device choice or dynamic
    // rendering were not what was asked for. Asserts on what the device
    // reports, not on a non-null pointer.
    engine::rhi::DeviceDesc desc{};
    desc.window_handle = nullptr;
    desc.width = 64;
    desc.height = 64;
    auto rhi = engine::rhi::vulkan::create_rhi();
    const bool factory_ok = rhi != nullptr && rhi->api() == engine::rhi::GraphicsAPI::Vulkan
        && rhi->name() == "Vulkan";
    auto device = rhi ? rhi->create_device(desc) : nullptr;

    // No Vulkan driver is an environment fact, not a defect. Skip by name, and
    // say `skip` rather than `pass` so the pass count cannot absorb it.
    if (factory_ok && !device) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "Vulkan device gate: no usable Vulkan device - needs a driver with dynamic "
            "rendering and synchronization2 (skip)");
        return true;
    }

    const bool offscreen_ok = device && device->offscreen();
    // SM 6.0 is what DXC compiles the SPIR-V from, so it is the honest figure
    // for a Vulkan device to report. feature_level has no Vulkan equivalent and
    // is deliberately 0 - GpuBaseline is D3D-shaped, and that is recorded
    // rather than filled with an unrelated number.
    const bool baseline_ok = device && device->gpu_baseline().shader_model >= 0x60
        && device->gpu_baseline().feature_level == 0;
    const bool not_lost = device && !device->device_lost();
    // A device-local heap has to exist and be non-trivial for anything to be
    // allocated later; 64 MiB is far below any real GPU and far above zero.
    const bool memory_ok = device && device->gpu_memory_stats().local_budget_bytes > (64u << 20);
    const bool passed = factory_ok && offscreen_ok && baseline_ok && not_lost && memory_ok;

    char message[224];
    std::snprintf(message, sizeof(message),
        "Vulkan device gate: factory=%s offscreen=%s sm=0x%02X fl=%u vram_mb=%llu lost=%s (%s)",
        factory_ok ? "yes" : "no", offscreen_ok ? "yes" : "no",
        device ? device->gpu_baseline().shader_model : 0u,
        device ? device->gpu_baseline().feature_level : 0u,
        device ? static_cast<unsigned long long>(
                     device->gpu_memory_stats().local_budget_bytes >> 20)
               : 0ull,
        not_lost ? "no" : "YES", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}
#endif // ENGINE_HAS_VULKAN

bool run_spirv_gate(engine::shaders::IShaderCompiler& compiler, const std::string& shader_path) {
    // Shaders #5. The DXC this engine ships - the Windows SDK's - cannot emit
    // SPIR-V at all: it answers "SPIR-V CodeGen not available", and nothing in
    // the DLL's strings says so, because the option table is compiled in
    // whether the backend is or not. So this asserts that a *different*,
    // SPIR-V-capable DXC was found and used, not merely that compile()
    // returned true.
    //
    // Four things:
    //   1. SPIR-V comes back with SPIR-V's magic word
    //   2. it is not the DXIL blob for the same shader, which catches both a
    //      wrong DLL and a cache key that stops folding the target
    //   3. DXIL still works from the same compiler instance - the proof that
    //      two same-named DXC builds coexist in one process
    //   4. a second SPIR-V request returns the same bytes, so the disk cache
    //      round-trips SPIR-V rather than serving the DXIL

    // Set when the compiler says no SPIR-V DXC exists on this machine, which is
    // an environment fact and not a defect. Matched on the message because the
    // alternative is new interface surface that exists only for this gate; it
    // is one string in one repository, and the compiler's own log line says the
    // same thing next to it.
    bool no_compiler = false;

    auto compile = [&](engine::shaders::ShaderTarget target,
                       engine::shaders::ShaderBytecode& out, bool& from_cache) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = "vs_main";
        desc.target_profile = "vs_6_0";
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        from_cache = compiler.last_compile_from_cache();
        if (!ok && error.find("No SPIR-V-capable DXC") != std::string::npos) {
            no_compiler = true;
        } else if (!ok && !error.empty()) {
            // Not for the absent-compiler case: the compiler already said so
            // once, with the fix in it, and repeating it per call turns one
            // fact into three lines.
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode spirv{};
    engine::shaders::ShaderBytecode dxil{};
    engine::shaders::ShaderBytecode spirv_again{};
    bool ignored = false;
    bool second_from_cache = false;
    const bool spirv_ok = compile(engine::shaders::ShaderTarget::Spirv, spirv, ignored);
    const bool dxil_ok = compile(engine::shaders::ShaderTarget::Dxil, dxil, ignored);
    const bool repeat_ok =
        compile(engine::shaders::ShaderTarget::Spirv, spirv_again, second_from_cache);

    if (no_compiler) {
        engine::log(engine::LogLevel::Warn, engine::LogChannel::Render,
            "SPIR-V gate: no SPIR-V-capable DXC on this machine - install the Vulkan SDK, "
            "or set ENGINE_DXC_SPIRV (skip)");
        return true;
    }

    engine::u32 spirv_magic = 0;
    engine::u32 dxil_magic = 0;
    if (spirv.data.size() >= 4) {
        std::memcpy(&spirv_magic, spirv.data.data(), 4);
    }
    if (dxil.data.size() >= 4) {
        std::memcpy(&dxil_magic, dxil.data.data(), 4);
    }

    const bool magic_ok = spirv_ok && spirv_magic == 0x07230203u;
    const bool dxil_still_ok = dxil_ok && dxil_magic == 0x43425844u;
    const bool distinct = spirv_ok && dxil_ok && spirv.data != dxil.data;
    const bool cache_ok = repeat_ok && spirv_again.data == spirv.data;

    const bool passed = magic_ok && dxil_still_ok && distinct && cache_ok;
    char message[240];
    std::snprintf(message, sizeof(message),
        "SPIR-V gate: spirv=0x%08X (%zu bytes) dxil=0x%08X (%zu bytes) distinct=%s "
        "round_trip=%s cached=%s (%s)",
        spirv_magic, spirv.data.size(), dxil_magic, dxil.data.size(), distinct ? "yes" : "no",
        cache_ok ? "yes" : "no", second_from_cache ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_parity_frames_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api) {
    // The first parity assertion about *time* rather than about a pixel.
    //
    // Four frames, each drawing with constants taken from the frame ring, with
    // no wait between them. That is the one thing the offscreen slice could
    // not have found: it submitted and waited inside a single frame, so one
    // command buffer, one fence and one descriptor pool were sufficient by
    // accident. With frames genuinely in flight, a pool reset or a command
    // buffer reused while the GPU is still reading it is a use-after-free that
    // shows up as garbage or a device loss.
    //
    // Four rather than three, so the run wraps past the slot count and reuses
    // slot 0 - the first reuse is where a missing fence wait bites.

    constexpr engine::u32 kExtent = 32;
    constexpr engine::u32 kFrames = 4;
    // One per frame, all exactly representable in UNORM8. The last frame's
    // value is what must survive, so a stale slot reads as an earlier frame's
    // colour rather than as nothing.
    constexpr engine::u8 kTints[kFrames] = {51, 102, 153, 204};

    auto compile = [&](const char* entry, const char* profile,
                       engine::shaders::ShaderBytecode& out) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = entry;
        desc.target_profile = profile;
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode vs{};
    engine::shaders::ShaderBytecode ps{};
    const bool compiled = compile("vs_main", "vs_6_0", vs) && compile("ps_main", "ps_6_0", ps);

    engine::rhi::TextureDesc color{};
    color.width = kExtent;
    color.height = kExtent;
    color.format = engine::rhi::Format::RGBA8_UNORM;
    color.usage = engine::rhi::TextureUsage::RenderTarget;
    auto target_texture = device.create_texture(color, nullptr);

    engine::rhi::GraphicsPipelineDesc pipeline{};
    pipeline.vertex_shader = std::span<const engine::u8>(vs.data);
    pipeline.pixel_shader = std::span<const engine::u8>(ps.data);
    pipeline.uniform_buffer_count = 1;
    pipeline.color_format = engine::rhi::Format::RGBA8_UNORM;
    pipeline.depth = engine::rhi::DepthTest::Disabled;
    pipeline.depth_write = false;
    pipeline.cull = engine::rhi::CullMode::None;
    pipeline.debug_name = "parity_frames";
    auto pso = compiled ? device.create_graphics_pipeline(pipeline) : nullptr;

    const engine::rhi::FrameRingStats before = device.frame_ring_stats();
    std::vector<engine::u8> pixels(static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    bool read_ok = false;
    engine::u32 ring_hits = 0;
    const bool ready = compiled && target_texture && pso;

    if (ready) {
        using State = engine::rhi::ResourceState;
        for (engine::u32 frame = 0; frame < kFrames; ++frame) {
            device.begin_frame();
            auto& cmd = device.command_list();
            cmd.begin();
            // Common only on the first frame; after that the texture is where
            // the previous frame left it, and saying Common again would be a
            // before-state that never happened.
            cmd.transition(*target_texture,
                frame == 0 ? State::Common : State::CopySrc, State::RenderTarget);

            const engine::f32 value = static_cast<engine::f32>(kTints[frame]) / 255.f;
            const engine::f32 constants[4] = {value, value, value, 1.f};
            const engine::rhi::FrameAllocation slice =
                device.alloc_frame_memory(sizeof(constants));
            if (slice.buffer != nullptr) {
                ++ring_hits;
                device.write_buffer(*slice.buffer, slice.offset, constants, sizeof(constants));
            }

            engine::rhi::RenderPassInfo pass{};
            pass.color = target_texture.get();
            pass.clear_color_target = true;
            pass.clear_depth = false;
            cmd.begin_render_pass(pass);
            cmd.set_pipeline(*pso);
            if (slice.buffer != nullptr) {
                cmd.set_constant_buffer(0, *slice.buffer, slice.offset);
            }
            cmd.draw(3, 0);
            cmd.end_render_pass();

            cmd.transition(*target_texture, State::RenderTarget, State::CopySrc);
            cmd.end();
            // Submitted and *not* waited. The next begin_frame waits its own
            // slot's fence, which is the whole mechanism under test.
            device.submit();
            device.end_frame();
        }
        device.wait_idle();
        read_ok = device.read_texture(*target_texture, pixels.data(), pixels.size());
    }

    const engine::rhi::FrameRingStats after = device.frame_ring_stats();
    // (8, 8) is inside the drawn half - the same probe the backend parity gate
    // uses, for the same shader.
    const engine::u8* probe = &pixels[(8 * static_cast<engine::usize>(kExtent) + 8) * 4];
    const bool last_frame_won = read_ok && probe[0] == kTints[kFrames - 1];
    const bool ring_served = ring_hits == kFrames;
    // The capacity is a backend constant and must be the same on both, because
    // the frame-ring budget gate compares headroom against it.
    const bool capacity_ok = after.capacity_bytes == 1024u * 1024u;
    const bool peak_moved = after.peak_bytes >= before.peak_bytes && after.peak_bytes > 0;
    const bool no_exhaustion = after.exhausted_frames == before.exhausted_frames;
    const bool passed = ready && read_ok && last_frame_won && ring_served && capacity_ok
        && peak_moved && no_exhaustion;

    char message[256];
    std::snprintf(message, sizeof(message),
        "Parity frames gate [%s]: frames=%u ring_hits=%u last=%u (want %u) peak=%llu "
        "capacity=%lluK exhausted=%llu (%s)",
        api, kFrames, ring_hits, probe[0], kTints[kFrames - 1],
        static_cast<unsigned long long>(after.peak_bytes),
        static_cast<unsigned long long>(after.capacity_bytes / 1024),
        static_cast<unsigned long long>(after.exhausted_frames), passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_parity_depth_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api) {
    // Depth parity under the device's own convention, which is reversed-Z
    // here: near is 1 and far is 0, and the compare, the clear value and the
    // sign of the bias all follow from that one field.
    //
    // Three full-target draws. The middle one is nearer and must win; the
    // third is at the first one's depth and must lose. That distinguishes four
    // failures that all look identical in a screenshot - no depth test (the
    // last draw wins), an inverted compare (the first wins), a clear value
    // that rejects everything (the clear colour survives), and depth writes
    // off (the last wins again).

    constexpr engine::u32 kExtent = 32;
    struct Constants {
        engine::f32 tint[4];
        engine::f32 params[4];
    };
    // 51, 153 and 204 exactly in UNORM8.
    constexpr engine::u8 kFirst = 51;
    constexpr engine::u8 kNearest = 153;
    constexpr engine::u8 kLast = 204;

    const bool reversed = device.depth_convention() == engine::rhi::DepthConvention::Reversed;
    // Derived from the convention, never hard-coded: writing 0.75 as "near"
    // would bake Reversed into the gate and pass for the wrong reason if the
    // device ever declared Standard.
    const engine::f32 far_z = reversed ? 0.25f : 0.75f;
    const engine::f32 near_z = reversed ? 0.75f : 0.25f;

    auto compile = [&](const char* entry, const char* profile,
                       engine::shaders::ShaderBytecode& out) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = entry;
        desc.target_profile = profile;
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode vs{};
    engine::shaders::ShaderBytecode ps{};
    const bool compiled = compile("vs_main", "vs_6_0", vs) && compile("ps_main", "ps_6_0", ps);

    engine::rhi::TextureDesc color{};
    color.width = kExtent;
    color.height = kExtent;
    color.format = engine::rhi::Format::RGBA8_UNORM;
    color.usage = engine::rhi::TextureUsage::RenderTarget;
    auto target_texture = device.create_texture(color, nullptr);

    engine::rhi::TextureDesc depth_desc{};
    depth_desc.width = kExtent;
    depth_desc.height = kExtent;
    depth_desc.format = engine::rhi::Format::D32_FLOAT;
    depth_desc.usage = engine::rhi::TextureUsage::DepthStencil;
    auto depth_texture = device.create_texture(depth_desc, nullptr);

    engine::rhi::GraphicsPipelineDesc pipeline{};
    pipeline.vertex_shader = std::span<const engine::u8>(vs.data);
    pipeline.pixel_shader = std::span<const engine::u8>(ps.data);
    pipeline.uniform_buffer_count = 1;
    pipeline.color_format = engine::rhi::Format::RGBA8_UNORM;
    pipeline.depth_format = engine::rhi::Format::D32_FLOAT;
    // Say the intent and derive the mechanism - depth_closer is what stops a
    // call site hard-coding Less and producing five-of-six reversed-Z.
    pipeline.depth = engine::rhi::depth_closer(device.depth_convention());
    pipeline.depth_write = true;
    pipeline.cull = engine::rhi::CullMode::None;
    pipeline.debug_name = "parity_depth";
    auto pso = compiled ? device.create_graphics_pipeline(pipeline) : nullptr;

    auto make_constants = [&](engine::u8 value, engine::f32 z) {
        Constants c{};
        c.tint[0] = static_cast<engine::f32>(value) / 255.f;
        c.tint[1] = c.tint[0];
        c.tint[2] = c.tint[0];
        c.tint[3] = 1.f;
        c.params[0] = z;
        engine::rhi::BufferDesc desc{};
        desc.size = sizeof(Constants);
        desc.usage = engine::rhi::BufferUsage::Uniform;
        return device.create_buffer(desc, &c);
    };
    auto first = make_constants(kFirst, far_z);
    auto nearest = make_constants(kNearest, near_z);
    auto last = make_constants(kLast, far_z);

    std::vector<engine::u8> pixels(static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    bool read_ok = false;
    const bool ready =
        compiled && target_texture && depth_texture && pso && first && nearest && last;

    if (ready) {
        using State = engine::rhi::ResourceState;
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.transition(*target_texture, State::Common, State::RenderTarget);
        // Not from Common: a depth texture is created in DepthWrite, which
        // the render graph knows and the contract now says. Transitioning from
        // Common is a barrier whose before-state never happened.
        (void)State::DepthWrite;

        engine::rhi::RenderPassInfo pass{};
        pass.color = target_texture.get();
        pass.depth = depth_texture.get();
        pass.clear_color_target = true;
        // Cleared to whatever the convention says is farthest, by the backend.
        // A backend that clears to the other end rejects every fragment and
        // the probe reads the clear colour.
        pass.clear_depth = true;
        cmd.begin_render_pass(pass);
        cmd.set_pipeline(*pso);
        for (engine::rhi::IBuffer* constants : {first.get(), nearest.get(), last.get()}) {
            cmd.set_constant_buffer(0, *constants);
            cmd.draw(3, 0);
        }
        cmd.end_render_pass();

        cmd.transition(*target_texture, State::RenderTarget, State::CopySrc);
        cmd.end();
        device.submit();
        device.wait_idle();
        read_ok = device.read_texture(*target_texture, pixels.data(), pixels.size());
    }

    const engine::u8* probe = &pixels[(16 * static_cast<engine::usize>(kExtent) + 16) * 4];
    const bool passed = ready && read_ok && probe[0] == kNearest;

    char message[224];
    std::snprintf(message, sizeof(message),
        "Parity depth gate [%s]: convention=%s near_z=%.2f got=%u (want %u, first=%u "
        "last=%u) (%s)",
        api, reversed ? "reversed" : "standard", static_cast<double>(near_z), probe[0],
        kNearest, kFirst, kLast, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_parity_texture_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api) {
    // Texture parity: one mip level and one cube face, both read at an exact
    // value. Those are the two things that go wrong quietly when uploading -
    // the mip offset and the array layer - so sampling mip 0 of a 2D texture
    // would prove almost nothing.
    //
    // The mip chain is written by hand rather than filtered, so mip 1 is a
    // value the gate chose. A backend that computes the wrong offset returns
    // mip 0's value, or garbage, and either is visibly not 153.

    constexpr engine::u32 kExtent = 32;
    // mip 0 is two black and two white texels, so its box filter is 127 -
    // a value none of its own texels has. That is what makes level 1 a real
    // assertion: reading 0 or 255 means the sampler fell back to level 0, and
    // reading 127 means the *generated* chain reached the GPU at level 1.
    //
    // These are the colour-space gate's own numbers, reused rather than
    // reinvented: it already asserts build_rgba8_mip_chain produces 127 for
    // a linear-format split like this one.
    constexpr engine::u8 kMip0Texel = 0;
    constexpr engine::u8 kMip1 = 127;
    constexpr engine::u8 kFace0 = 204;
    constexpr engine::u8 kOtherFace = 102;

    // Mip 0 only. For a single-layer RGBA8 2D texture the contract says `data`
    // is level 0 and the backend generates the rest - supplying a full chain
    // here would be silently refiltered over the top, which is how this gate's
    // first version read 51 from level 1.
    const engine::u8 albedo_bytes[2 * 2 * 4] = {
        0, 0, 0, 255,          255, 255, 255, 255,
        0, 0, 0, 255,          255, 255, 255, 255,
    };

    // Six 2x2 faces, slice-major, the whole chain - because a cube is not the
    // generated case. Face 0 is +X and the only one the shader reads; the rest
    // carry a different value, so wrong layer indexing reads 102 rather than
    // merely reading nothing.
    engine::u8 cube_bytes[6 * 2 * 2 * 4]{};
    for (engine::u32 face = 0; face < 6; ++face) {
        const engine::u8 value = face == 0 ? kFace0 : kOtherFace;
        for (engine::u32 i = 0; i < 4; ++i) {
            engine::u8* texel = &cube_bytes[(face * 4 + i) * 4];
            texel[0] = value;
            texel[1] = value;
            texel[2] = value;
            texel[3] = 255;
        }
    }

    auto compile = [&](const char* entry, const char* profile,
                       engine::shaders::ShaderBytecode& out) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = entry;
        desc.target_profile = profile;
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode vs{};
    engine::shaders::ShaderBytecode ps{};
    const bool compiled = compile("vs_main", "vs_6_0", vs) && compile("ps_main", "ps_6_0", ps);

    engine::rhi::TextureDesc albedo_desc{};
    albedo_desc.width = 2;
    albedo_desc.height = 2;
    albedo_desc.mip_levels = 2;
    albedo_desc.format = engine::rhi::Format::RGBA8_UNORM;
    albedo_desc.usage = engine::rhi::TextureUsage::ShaderResource;
    auto albedo = device.create_texture(albedo_desc, albedo_bytes);

    engine::rhi::TextureDesc cube_desc{};
    cube_desc.width = 2;
    cube_desc.height = 2;
    cube_desc.array_size = 6;
    cube_desc.dimension = engine::rhi::TextureDimension::Cube;
    cube_desc.format = engine::rhi::Format::RGBA8_UNORM;
    cube_desc.usage = engine::rhi::TextureUsage::ShaderResource;
    auto cube = device.create_texture(cube_desc, cube_bytes);

    engine::rhi::TextureDesc color{};
    color.width = kExtent;
    color.height = kExtent;
    color.format = engine::rhi::Format::RGBA8_UNORM;
    color.usage = engine::rhi::TextureUsage::RenderTarget;
    auto target_texture = device.create_texture(color, nullptr);

    engine::rhi::GraphicsPipelineDesc pipeline{};
    pipeline.vertex_shader = std::span<const engine::u8>(vs.data);
    pipeline.pixel_shader = std::span<const engine::u8>(ps.data);
    pipeline.sampled_texture_count = 2;
    // Immutable, from the desc - the contract's rule, and there is no
    // set_sampler to bind one per draw.
    // Point, so a probe reads one texel instead of a blend of four - the
    // whole point is which level was read, not how it was filtered.
    pipeline.samplers[0] = engine::rhi::point_clamp_sampler();
    pipeline.sampler_count = 1;
    pipeline.color_format = engine::rhi::Format::RGBA8_UNORM;
    pipeline.depth = engine::rhi::DepthTest::Disabled;
    pipeline.depth_write = false;
    pipeline.cull = engine::rhi::CullMode::None;
    pipeline.debug_name = "parity_texture";
    auto pso = compiled ? device.create_graphics_pipeline(pipeline) : nullptr;

    std::vector<engine::u8> pixels(static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    bool read_ok = false;
    const bool ready = compiled && albedo && cube && target_texture && pso;

    if (ready) {
        using State = engine::rhi::ResourceState;
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.transition(*target_texture, State::Common, State::RenderTarget);

        engine::rhi::RenderPassInfo pass{};
        pass.color = target_texture.get();
        pass.clear_color_target = true;
        pass.clear_depth = false;
        cmd.begin_render_pass(pass);
        cmd.set_pipeline(*pso);
        // Two sampled textures and a uniform-free pipeline: the point at which
        // one descriptor set has to carry more than one binding.
        cmd.set_shader_resource(0, *albedo);
        cmd.set_shader_resource(1, *cube);
        cmd.draw(3, 0);
        cmd.end_render_pass();

        cmd.transition(*target_texture, State::RenderTarget, State::CopySrc);
        cmd.end();
        device.submit();
        device.wait_idle();
        read_ok = device.read_texture(*target_texture, pixels.data(), pixels.size());
    }

    const engine::u8* probe = &pixels[(16 * static_cast<engine::usize>(kExtent) + 16) * 4];
    const bool mip_ok = read_ok && probe[0] == kMip1;
    const bool face_ok = read_ok && probe[1] == kFace0;
    const bool base_ok = read_ok && probe[2] == kMip0Texel;
    const bool passed = ready && read_ok && mip_ok && face_ok && base_ok;

    char message[240];
    std::snprintf(message, sizeof(message),
        "Parity texture gate [%s]: mip1=%u (want %u) cube_face0=%u (want %u) mip0=%u "
        "(want %u) (%s)",
        api, probe[0], kMip1, probe[1], kFace0, probe[2], kMip0Texel,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_parity_mesh_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api) {
    // Vertex input parity, one function against both devices - the same shape
    // run_backend_parity_gate uses, and for the same reason: a divergence has
    // to be a backend difference and cannot be a difference in the test.
    //
    // An indexed quad whose per-vertex data is *used*. The four probe texels
    // land one per quadrant, and each channel answers a different question:
    //   R, G  the uv reached the shader, and which way round it is
    //   B     the normal reached the shader at all
    // Swap the two attribute locations and the quadrants collapse to one
    // colour; lose the index buffer and the probes read the clear colour.

    constexpr engine::u32 kExtent = 64;

    struct Vertex {
        engine::f32 pos[3];
        engine::f32 normal[3];
        engine::f32 uv[2];
    };
    // Clip space directly, corner to corner, so there is no transform to be
    // wrong about. Top-left, top-right, bottom-left, bottom-right.
    static const Vertex kVertices[4] = {
        {{-1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 0.f}},
        {{1.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 0.f}},
        {{-1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f}},
        {{1.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, {1.f, 1.f}},
    };
    // Two triangles, clockwise to match the front face both pipelines declare.
    //
    // u32 because that is the only width the contract supports - see
    // set_index_buffer on ICommandList. The first version of this gate used
    // u16 and drew nothing, on *both* backends, which is how the implicit
    // 32-bit rule got written down.
    static const engine::u32 kIndices[6] = {0, 1, 2, 2, 1, 3};

    auto compile = [&](const char* entry, const char* profile,
                       engine::shaders::ShaderBytecode& out) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = entry;
        desc.target_profile = profile;
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode vs{};
    engine::shaders::ShaderBytecode ps{};
    const bool compiled = compile("vs_main", "vs_6_0", vs) && compile("ps_main", "ps_6_0", ps);

    engine::rhi::TextureDesc color{};
    color.width = kExtent;
    color.height = kExtent;
    color.format = engine::rhi::Format::RGBA8_UNORM;
    color.usage = engine::rhi::TextureUsage::RenderTarget;
    auto target_texture = device.create_texture(color, nullptr);

    engine::rhi::BufferDesc vertex_desc{};
    vertex_desc.size = sizeof(kVertices);
    vertex_desc.usage = engine::rhi::BufferUsage::Vertex;
    auto vertex_buffer = device.create_buffer(vertex_desc, kVertices);

    engine::rhi::BufferDesc index_desc{};
    index_desc.size = sizeof(kIndices);
    index_desc.usage = engine::rhi::BufferUsage::Index;
    auto index_buffer = device.create_buffer(index_desc, kIndices);

    engine::rhi::GraphicsPipelineDesc pipeline{};
    pipeline.vertex_shader = std::span<const engine::u8>(vs.data);
    pipeline.pixel_shader = std::span<const engine::u8>(ps.data);
    // Declaration order matters: index 0 is the shader's first semantic. See
    // the shader's own comment.
    pipeline.attributes[0] = {engine::rhi::VertexSemantic::Position, 0,
        engine::rhi::VertexFormat::Float3, offsetof(Vertex, pos)};
    pipeline.attributes[1] = {engine::rhi::VertexSemantic::Normal, 0,
        engine::rhi::VertexFormat::Float3, offsetof(Vertex, normal)};
    pipeline.attributes[2] = {engine::rhi::VertexSemantic::TexCoord, 0,
        engine::rhi::VertexFormat::Float2, offsetof(Vertex, uv)};
    pipeline.attribute_count = 3;
    pipeline.color_format = engine::rhi::Format::RGBA8_UNORM;
    pipeline.depth = engine::rhi::DepthTest::Disabled;
    pipeline.depth_write = false;
    pipeline.cull = engine::rhi::CullMode::None;
    pipeline.debug_name = "parity_mesh";
    auto pso = compiled ? device.create_graphics_pipeline(pipeline) : nullptr;

    std::vector<engine::u8> pixels(static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    bool read_ok = false;
    const bool ready = compiled && target_texture && vertex_buffer && index_buffer && pso;

    if (ready) {
        using State = engine::rhi::ResourceState;
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.transition(*target_texture, State::Common, State::RenderTarget);

        engine::rhi::RenderPassInfo pass{};
        pass.color = target_texture.get();
        pass.clear_color_target = true;
        pass.clear_depth = false;
        cmd.begin_render_pass(pass);
        cmd.set_pipeline(*pso);
        cmd.set_vertex_buffer(0, *vertex_buffer, sizeof(Vertex));
        cmd.set_index_buffer(*index_buffer);
        cmd.draw_indexed(6, 0, 0, 1);
        cmd.end_render_pass();

        cmd.transition(*target_texture, State::RenderTarget, State::CopySrc);
        cmd.end();
        device.submit();
        device.wait_idle();
        read_ok = device.read_texture(*target_texture, pixels.data(), pixels.size());
    }

    auto texel = [&pixels](engine::u32 x, engine::u32 y) {
        return &pixels[(static_cast<engine::usize>(y) * kExtent + x) * 4];
    };
    // 0.2, 0.6 and 0.8 are exactly 51, 153 and 204 in UNORM8.
    struct Probe {
        engine::u32 x;
        engine::u32 y;
        engine::u8 expect[4];
    };
    static const Probe kProbes[4] = {
        {8, 8, {51, 51, 255, 255}},
        {55, 8, {204, 51, 255, 255}},
        {8, 55, {51, 153, 255, 255}},
        {55, 55, {204, 153, 255, 255}},
    };
    engine::u32 matched = 0;
    if (read_ok) {
        for (const Probe& probe : kProbes) {
            const engine::u8* t = texel(probe.x, probe.y);
            if (t[0] == probe.expect[0] && t[1] == probe.expect[1] && t[2] == probe.expect[2]
                && t[3] == probe.expect[3]) {
                ++matched;
            }
        }
    }

    const engine::u8* first = texel(kProbes[0].x, kProbes[0].y);
    const engine::u8* last = texel(kProbes[3].x, kProbes[3].y);
    const bool passed = ready && read_ok && matched == 4;

    char message[256];
    std::snprintf(message, sizeof(message),
        "Parity mesh gate [%s]: quadrants=%u/4 tl=(%u,%u,%u,%u) br=(%u,%u,%u,%u) (%s)", api,
        matched, first[0], first[1], first[2], first[3], last[0], last[1], last[2], last[3],
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_backend_parity_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path,
    engine::shaders::ShaderTarget target, const char* api, engine::u32& lit_out) {
    // The gate that makes "the contract survives a second backend" checkable
    // rather than asserted. One function, one shader, two devices - so a
    // divergence is a backend difference and cannot be a difference in the
    // test.
    //
    // Five things, none of which is "it did not crash":
    //   1. the device reports itself offscreen
    //   2. the bytecode's magic word matches the target that was asked for, so
    //      a backend can never be handed the other API's bytecode
    //   3. an interior texel of the drawn half carries the cbuffer's tint,
    //      byte-exact - which covers pipeline, descriptor binding and readback
    //      in one value
    //   4. the mirrored texel across the diagonal is still the clear colour.
    //      This is the Y-direction check: a backend that flips it draws the
    //      other half, and a coverage count would average that away
    //   5. the covered-texel count, returned rather than asserted - the caller
    //      compares the two backends, because only both numbers together mean
    //      anything

    constexpr engine::u32 kExtent = 64;
    // Exactly representable in UNORM8, so no channel sits on a .5 rounding tie
    // and the readback can be an equality instead of a tolerance.
    constexpr engine::f32 kTint[4] = {51.f / 255.f, 153.f / 255.f, 204.f / 255.f, 1.f};
    constexpr engine::u8 kTintBytes[4] = {51, 153, 204, 255};

    lit_out = 0;

    auto compile = [&](const char* entry, const char* profile,
                       engine::shaders::ShaderBytecode& out) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = shader_path;
        desc.entry_point = entry;
        desc.target_profile = profile;
        desc.target = target;
        std::string error;
        const bool ok = compiler.compile(desc, out, error) && !out.data.empty();
        if (!ok && !error.empty()) {
            engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
        }
        return ok;
    };

    engine::shaders::ShaderBytecode vs{};
    engine::shaders::ShaderBytecode ps{};
    const bool compiled = compile("vs_main", "vs_6_0", vs) && compile("ps_main", "ps_6_0", ps);

    engine::u32 magic = 0;
    if (vs.data.size() >= 4) {
        std::memcpy(&magic, vs.data.data(), 4);
    }
    // 'DXBC' little-endian for DXIL containers, SPIR-V's own magic otherwise.
    const engine::u32 expected_magic =
        target == engine::shaders::ShaderTarget::Spirv ? 0x07230203u : 0x43425844u;
    const bool bytecode_ok = compiled && magic == expected_magic;

    engine::rhi::TextureDesc color{};
    color.width = kExtent;
    color.height = kExtent;
    color.format = engine::rhi::Format::RGBA8_UNORM;
    color.usage = engine::rhi::TextureUsage::RenderTarget;
    auto target_texture = device.create_texture(color, nullptr);

    engine::rhi::BufferDesc constants{};
    constants.size = sizeof(engine::f32) * 4;
    constants.usage = engine::rhi::BufferUsage::Uniform;
    auto tint_buffer = device.create_buffer(constants, kTint);

    engine::rhi::GraphicsPipelineDesc pipeline{};
    pipeline.vertex_shader = std::span<const engine::u8>(vs.data);
    pipeline.pixel_shader = std::span<const engine::u8>(ps.data);
    pipeline.uniform_buffer_count = 1;
    pipeline.color_format = engine::rhi::Format::RGBA8_UNORM;
    pipeline.depth = engine::rhi::DepthTest::Disabled;
    pipeline.depth_write = false;
    pipeline.cull = engine::rhi::CullMode::None;
    pipeline.debug_name = "backend_parity";
    auto pso = bytecode_ok ? device.create_graphics_pipeline(pipeline) : nullptr;

    std::vector<engine::u8> pixels(static_cast<engine::usize>(kExtent) * kExtent * 4, 0);
    bool read_ok = false;
    const bool ready = bytecode_ok && target_texture && tint_buffer && pso;

    if (ready) {
        using State = engine::rhi::ResourceState;
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.transition(*target_texture, State::Common, State::RenderTarget);

        engine::rhi::RenderPassInfo pass{};
        pass.color = target_texture.get();
        pass.clear_color_target = true;
        pass.clear_depth = false;
        cmd.begin_render_pass(pass);
        cmd.set_pipeline(*pso);
        cmd.set_constant_buffer(0, *tint_buffer);
        cmd.draw(3, 0);
        cmd.end_render_pass();

        cmd.transition(*target_texture, State::RenderTarget, State::CopySrc);
        cmd.end();
        device.submit();
        device.wait_idle();
        read_ok = device.read_texture(*target_texture, pixels.data(), pixels.size());
    }

    // (8, 8) is inside the drawn half; (55, 55) is its mirror in the half that
    // must stay clear. An inverted Y swaps which is which.
    auto texel = [&pixels](engine::u32 x, engine::u32 y) {
        return &pixels[(static_cast<engine::usize>(y) * kExtent + x) * 4];
    };
    const engine::u8* inside = texel(8, 8);
    const engine::u8* outside = texel(55, 55);
    const bool tint_ok = read_ok && inside[0] == kTintBytes[0] && inside[1] == kTintBytes[1]
        && inside[2] == kTintBytes[2] && inside[3] == kTintBytes[3];
    const bool clear_ok = read_ok && outside[0] == 0 && outside[1] == 0 && outside[2] == 0
        && outside[3] == 255;

    engine::u32 lit = 0;
    if (read_ok) {
        lit = count_lit_texels(pixels.data(), kExtent, kExtent);
    }
    lit_out = lit;
    // Half the target, give or take the diagonal's own width. Derived from the
    // geometry, not from whatever the first run happened to print.
    const bool coverage_ok = lit > (kExtent * kExtent / 2) - 2 * kExtent
        && lit < (kExtent * kExtent / 2) + 2 * kExtent;

    const bool offscreen_ok = device.offscreen();
    const bool passed =
        offscreen_ok && bytecode_ok && read_ok && tint_ok && clear_ok && coverage_ok;

    char message[256];
    std::snprintf(message, sizeof(message),
        "Backend parity gate [%s]: offscreen=%s magic=0x%08X inside=(%u,%u,%u,%u) "
        "outside=(%u,%u,%u,%u) lit=%u (%s)",
        api, offscreen_ok ? "yes" : "no", magic, inside[0], inside[1], inside[2], inside[3],
        outside[0], outside[1], outside[2], outside[3], lit, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_pix_gate(engine::rhi::IDevice* device) {
    if (!device) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Pix gate: no device (FAIL)");
        return false;
    }

    device->begin_frame();
    auto& cmd = device->command_list();
    cmd.begin();
    const bool start_ok = cmd.debug_event_depth() == 0;

    cmd.begin_event("gate/outer");
    const bool begin_ok = start_ok
        && cmd.debug_event_depth() == 1
        && cmd.debug_event_name() == "gate/outer";

    cmd.begin_event("gate/inner");
    const bool nest_ok = cmd.debug_event_depth() == 2
        && cmd.debug_event_name() == "gate/inner";

    cmd.set_marker("gate/tick");
    const bool marker_ok = cmd.last_debug_marker() == "gate/tick";

    cmd.end_event();
    cmd.end_event();
    const bool depth_ok = cmd.debug_event_depth() == 0
        && cmd.debug_event_name().empty();

    cmd.end();
    device->submit();
    device->wait_idle();

    const bool passed = begin_ok && nest_ok && marker_ok && depth_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Pix gate: begin=%s nest=%s marker=%s depth=%s (%s)",
        begin_ok ? "yes" : "no",
        nest_ok ? "yes" : "no",
        marker_ok ? "yes" : "no",
        depth_ok ? "0" : "bad",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

static bool bytecode_is_dxil(const std::vector<engine::u8>& data) {
    static constexpr char kDxil[] = {'D', 'X', 'I', 'L'};
    if (data.size() < 4) {
        return false;
    }
    return std::search(data.begin(), data.end(), kDxil, kDxil + 4) != data.end();
}

static bool bytecode_is_spirv(const std::vector<engine::u8>& data) {
    if (data.size() < 4) {
        return false;
    }
    engine::u32 magic = 0;
    std::memcpy(&magic, data.data(), sizeof(magic));
    return magic == 0x07230203u;
}

// Does the blob match the kind that was asked for?
//
// This replaced a bare `is it DXIL` in two gates, and it is the stronger
// assertion rather than the looser one: it fails both ways round. Asking for
// SPIR-V and getting DXIL is the defect that cost the whole forward pass on a
// second backend - every pipeline rejected the bytecode - and asking for DXIL
// and getting SPIR-V would be just as wrong while a DXIL-only check called it
// a pass.
static bool bytecode_matches_target(
    const std::vector<engine::u8>& data, engine::shaders::ShaderTarget target) {
    return target == engine::shaders::ShaderTarget::Spirv ? bytecode_is_spirv(data)
                                                          : bytecode_is_dxil(data);
}

static const char* target_name(engine::shaders::ShaderTarget target) {
    return target == engine::shaders::ShaderTarget::Spirv ? "spirv" : "dxil";
}

bool run_shader_cache_gate(engine::shaders::IShaderCompiler& compiler,
    const engine::shaders::ShaderCompileDesc& desc) {
    engine::shaders::ShaderBytecode first;
    engine::shaders::ShaderBytecode second;
    std::string error;
    if (!compiler.compile(desc, first, error) || first.data.empty()) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            "Shader cache gate compile failed");
        return false;
    }
    const bool first_hit = compiler.last_compile_from_cache();
    if (!compiler.compile(desc, second, error) || second.data.empty()) {
        return false;
    }
    const bool second_hit = compiler.last_compile_from_cache();
    // The kind that was asked for, not DXIL. The cache key folds in
    // desc.target (shader_cache_dxc.cpp), so a cache that ignored the target
    // would hand back the other backend's blob on the second call - which is
    // exactly what this gate is placed to catch.
    const bool kind_ok = bytecode_matches_target(first.data, desc.target);
    const bool passed = second_hit && first.data.size() == second.data.size() && kind_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Shader cache gate: first=%s second=%s asked=%s got=%s bytes=%zu (%s)",
        first_hit ? "hit" : "miss",
        second_hit ? "hit" : "miss",
        target_name(desc.target),
        kind_ok ? target_name(desc.target) : "other",
        first.data.size(),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_dxc_gate(const engine::shaders::ShaderCompileDesc& desc,
    const engine::shaders::ShaderBytecode& bytecode) {
    // Still the DXC gate on both backends: the SPIR-V path is a second DXC,
    // not a second compiler vendor. What it used to assert - that the target
    // *is* DXIL - was a restatement of the sandbox's own hard-coded choice, so
    // it could not fail. Asserting the blob matches whatever was asked for can.
    const bool sm6 = desc.target_profile.find("6_") != std::string::npos;
    const bool kind_ok = bytecode_matches_target(bytecode.data, desc.target);
    const bool passed = sm6 && kind_ok && !bytecode.data.empty();
    char message[192];
    std::snprintf(message, sizeof(message),
        "DXC gate: profile=%s asked=%s got=%s bytes=%zu (%s)",
        desc.target_profile.c_str(), target_name(desc.target),
        kind_ok ? target_name(desc.target) : "other", bytecode.data.size(),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_rhi_contract_gate(
    engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler) {
    auto sampler =
        device.create_sampler(engine::rhi::shadow_comparison_sampler(device.depth_convention()));
    const bool sampler_ok = sampler != nullptr
        && engine::rhi::GraphicsPipelineDesc::kMaxSamplers >= 2;

    // This used to assert that SPIR-V was *rejected*, which was true and worth
    // checking for as long as no SPIR-V compiler existed. Shaders #5 made it
    // false, and the gate went red - correctly. What survives is the property
    // that outlives the change: a source file that does not exist is refused
    // for either target, rather than one of them reporting success on nothing.
    // Whether SPIR-V actually works is run_spirv_gate's job now.
    engine::shaders::ShaderBytecode unused;
    std::string error;
    bool missing_rejected = true;
    for (const engine::shaders::ShaderTarget target :
        {engine::shaders::ShaderTarget::Dxil, engine::shaders::ShaderTarget::Spirv}) {
        engine::shaders::ShaderCompileDesc desc{};
        desc.file_path = "does_not_exist.hlsl";
        desc.entry_point = "main";
        desc.target_profile = "vs_6_0";
        desc.target = target;
        missing_rejected = missing_rejected && !compiler.compile(desc, unused, error);
    }
    const bool target_default =
        engine::shaders::ShaderCompileDesc{}.target == engine::shaders::ShaderTarget::Dxil;

    const bool passed = sampler_ok && missing_rejected && target_default;
    char message[224];
    std::snprintf(message, sizeof(message),
        "RHI contract gate: sampler=%s missing_file_rejected=%s target_enum=%s (%s)",
        sampler_ok ? "yes" : "no",
        missing_rejected ? "yes" : "no", target_default ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

static void fill_rgba8(std::vector<engine::u8>& pixels, engine::u32 pixel_count, engine::u8 r,
    engine::u8 g, engine::u8 b) {
    pixels.resize(static_cast<size_t>(pixel_count) * 4);
    for (engine::u32 i = 0; i < pixel_count; ++i) {
        pixels[static_cast<size_t>(i) * 4 + 0] = r;
        pixels[static_cast<size_t>(i) * 4 + 1] = g;
        pixels[static_cast<size_t>(i) * 4 + 2] = b;
        pixels[static_cast<size_t>(i) * 4 + 3] = 255;
    }
}

bool run_color_space_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& srgb_gate_path) {
    // 1 + 2: the curve itself.
    const engine::f32 mid_linear = engine::math::srgb_to_linear(0.5f);
    const bool mid_ok = std::fabs(mid_linear - 0.2140411f) < 1.e-5f;

    const engine::f32 toe = engine::math::linear_to_srgb(0.001f);
    const bool toe_ok = std::fabs(toe - 0.01292f) < 1.e-6f;

    bool round_trip_ok = true;
    for (const engine::f32 v : {0.f, 0.02f, 0.2f, 0.5f, 0.8f, 1.f}) {
        const engine::f32 back = engine::math::srgb_to_linear(engine::math::linear_to_srgb(v));
        round_trip_ok = round_trip_ok && std::fabs(back - v) < 1.e-4f;
    }

    // 3: mip averaging. Two black texels, two white; alpha split the same way.
    const engine::u8 top[16] = {
        0, 0, 0, 0,        255, 255, 255, 255,
        0, 0, 0, 0,        255, 255, 255, 255,
    };
    const std::vector<engine::u8> srgb_chain =
        engine::math::build_rgba8_mip_chain(top, 2, 2, 2, true);
    const std::vector<engine::u8> linear_chain =
        engine::math::build_rgba8_mip_chain(top, 2, 2, 2, false);
    const bool chain_size_ok = srgb_chain.size() == 20 && linear_chain.size() == 20;
    // mip 1 is the single texel after the 16-byte top level.
    const engine::u32 srgb_rgb = chain_size_ok ? srgb_chain[16] : 0;
    const engine::u32 srgb_alpha = chain_size_ok ? srgb_chain[19] : 0;
    const engine::u32 linear_rgb = chain_size_ok ? linear_chain[16] : 0;
    const bool mip_ok = chain_size_ok && srgb_rgb == 188 && srgb_alpha == 127
        && linear_rgb == 127;

    // 4: the HLSL curve, through a UAV readback.
    engine::shaders::ShaderCompileDesc cs_desc{};
    cs_desc.file_path = srgb_gate_path;
    cs_desc.entry_point = "cs_main";
    cs_desc.target_profile = "cs_6_0";
    cs_desc.target = shader_target_for(device);
    engine::shaders::ShaderBytecode cs_bytecode;
    std::string error;
    const bool compiled =
        compiler.compile(cs_desc, cs_bytecode, error) && !cs_bytecode.data.empty();
    if (!compiled && !error.empty()) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render, error);
    }

    engine::rhi::ComputePipelineDesc compute{};
    compute.compute_shader = compiled ? std::span<const engine::u8>(cs_bytecode.data)
                                      : std::span<const engine::u8>{};
    compute.storage_texture_count = 1;
    compute.debug_name = "srgb_gate";
    auto pso = compiled ? device.create_compute_pipeline(compute) : nullptr;

    constexpr engine::usize kProbeBytes = 4 * sizeof(engine::u32);
    engine::rhi::BufferDesc storage{};
    storage.size = kProbeBytes;
    storage.usage = engine::rhi::BufferUsage::Storage;
    auto rw = device.create_buffer(storage);

    engine::rhi::BufferDesc readback{};
    readback.size = kProbeBytes;
    readback.usage = engine::rhi::BufferUsage::Readback;
    auto rb = device.create_buffer(readback);

    engine::f32 probes[4]{};
    bool hlsl_ok = false;
    if (pso && rw && rb) {
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.set_compute_pipeline(*pso);
        cmd.set_unordered_access(0, *rw);
        cmd.dispatch(1, 1, 1);
        cmd.transition(*rw, engine::rhi::ResourceState::Storage,
            engine::rhi::ResourceState::CopySrc);
        cmd.copy_buffer(*rw, *rb, kProbeBytes);
        cmd.end();
        device.submit();
        device.wait_idle();
        device.read_buffer(*rb, 0, probes, kProbeBytes);
        hlsl_ok = std::fabs(probes[0] - 0.5f) < 2.e-3f
            && std::fabs(probes[1] - 0.01292f) < 1.e-4f
            && std::fabs(probes[2] - 0.2140411f) < 2.e-3f
            && std::fabs(probes[3] - 0.7f) < 2.e-3f;
    }

    const bool passed = mid_ok && toe_ok && round_trip_ok && mip_ok && hlsl_ok;
    char message[256];
    std::snprintf(message, sizeof(message),
        "Colour space gate: mid=%.6f toe=%.5f round_trip=%s mip_srgb=%u mip_linear=%u "
        "mip_alpha=%u hlsl=(%.4f,%.5f,%.4f,%.4f) (%s)",
        static_cast<double>(mid_linear), static_cast<double>(toe),
        round_trip_ok ? "yes" : "no", srgb_rgb, linear_rgb, srgb_alpha,
        static_cast<double>(probes[0]), static_cast<double>(probes[1]),
        static_cast<double>(probes[2]), static_cast<double>(probes[3]),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_rhi_impl_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler,
    const std::string& compute_path) {
    engine::shaders::ShaderCompileDesc cs_desc{};
    cs_desc.file_path = compute_path;
    cs_desc.entry_point = "cs_main";
    cs_desc.target_profile = "cs_6_0";
    cs_desc.target = shader_target_for(device);
    engine::shaders::ShaderBytecode cs_bytecode;
    std::string error;
    const bool compiled
        = compiler.compile(cs_desc, cs_bytecode, error) && !cs_bytecode.data.empty();

    engine::rhi::ComputePipelineDesc compute{};
    compute.compute_shader = compiled ? std::span<const engine::u8>(cs_bytecode.data)
                                      : std::span<const engine::u8>{};
    compute.storage_texture_count = 1;
    compute.debug_name = "compute_gate";
    auto compute_pso = compiled ? device.create_compute_pipeline(compute) : nullptr;
    const bool pso_ok = compute_pso != nullptr;

    engine::rhi::BufferDesc storage{};
    storage.size = 4;
    storage.usage = engine::rhi::BufferUsage::Storage;
    auto rw = device.create_buffer(storage);

    engine::rhi::BufferDesc readback{};
    readback.size = 4;
    readback.usage = engine::rhi::BufferUsage::Readback;
    auto rb = device.create_buffer(readback);

    engine::u32 magic = 0;
    bool dispatch_ok = false;
    if (pso_ok && rw && rb) {
        device.begin_frame();
        auto& cmd = device.command_list();
        cmd.begin();
        cmd.set_compute_pipeline(*compute_pso);
        cmd.set_unordered_access(0, *rw);
        cmd.dispatch(1, 1, 1);
        cmd.transition(*rw, engine::rhi::ResourceState::Storage,
            engine::rhi::ResourceState::CopySrc);
        cmd.copy_buffer(*rw, *rb, 4);
        cmd.end();
        device.submit();
        device.wait_idle();
        device.read_buffer(*rb, 0, &magic, sizeof(magic));
        dispatch_ok = magic == kComputeGateMagic;
    }

    constexpr engine::u32 kFace = 4;
    constexpr engine::u32 kFacePixels = kFace * kFace;
    std::vector<engine::u8> cube_pixels(6 * kFacePixels * 4);
    const engine::u8 face_colors[6][3] = {
        {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {255, 0, 255}, {0, 255, 255},
    };
    for (engine::u32 face = 0; face < 6; ++face) {
        for (engine::u32 i = 0; i < kFacePixels; ++i) {
            const size_t o = (static_cast<size_t>(face) * kFacePixels + i) * 4;
            cube_pixels[o + 0] = face_colors[face][0];
            cube_pixels[o + 1] = face_colors[face][1];
            cube_pixels[o + 2] = face_colors[face][2];
            cube_pixels[o + 3] = 255;
        }
    }
    engine::rhi::TextureDesc cube{};
    cube.width = kFace;
    cube.height = kFace;
    cube.mip_levels = 1;
    cube.array_size = 6;
    cube.dimension = engine::rhi::TextureDimension::Cube;
    cube.format = engine::rhi::Format::RGBA8_UNORM;
    cube.usage = engine::rhi::TextureUsage::ShaderResource;
    auto cube_tex = device.create_texture(cube, cube_pixels.data());
    const bool cube_ok = cube_tex
        && cube_tex->array_size() == 6
        && cube_tex->dimension() == engine::rhi::TextureDimension::Cube;

    std::vector<engine::u8> array_pixels;
    fill_rgba8(array_pixels, kFacePixels * 4, 32, 64, 128);
    engine::rhi::TextureDesc array_desc{};
    array_desc.width = kFace;
    array_desc.height = kFace;
    array_desc.mip_levels = 1;
    array_desc.array_size = 4;
    array_desc.dimension = engine::rhi::TextureDimension::Tex2DArray;
    array_desc.format = engine::rhi::Format::RGBA8_UNORM;
    array_desc.usage = engine::rhi::TextureUsage::ShaderResource;
    auto array_tex = device.create_texture(array_desc, array_pixels.data());
    const bool array_ok = array_tex
        && array_tex->array_size() == 4
        && array_tex->dimension() == engine::rhi::TextureDimension::Tex2DArray;

    const bool passed = pso_ok && dispatch_ok && cube_ok && array_ok;
    char message[256];
    std::snprintf(message, sizeof(message),
        "RHI impl gate: compute=%s dispatch=%s cubes=%s arrays=%s (%s)",
        pso_ok ? "yes" : "no", dispatch_ok ? "yes" : "no",
        cube_ok ? "yes" : "no", array_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    if (!compiled) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Render,
            error.empty() ? "compute_gate.hlsl compile failed" : error.c_str());
    }
    return passed;
}

bool run_mip_gate(const engine::rhi::ITexture& texture, engine::u32 source_width) {
    engine::u32 expected = 1;
    engine::u32 dim = source_width;
    while (dim > 1) {
        dim /= 2;
        expected += 1;
    }
    const bool passed = texture.mip_levels() >= 8 && texture.mip_levels() == expected
        && texture.width() == source_width;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Mip gate: %ux%u mips=%u expected=%u (%s)",
        texture.width(), texture.height(), texture.mip_levels(), expected,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_aspect_gate() {
    const engine::f32 fov = engine::math::radians(60.f);
    const engine::f32 aspect = 1280.f / 720.f;
    const engine::math::Mat4 projection = engine::math::Mat4::perspective(fov, aspect, 0.1f, 100.f);
    const engine::f32 expected = 1.f / (aspect * std::tan(fov * 0.5f));
    const engine::math::Mat4 wide = engine::math::Mat4::perspective(fov, 16.f / 9.f, 0.1f, 100.f);
    const engine::math::Mat4 tall = engine::math::Mat4::perspective(fov, 4.f / 3.f, 0.1f, 100.f);
    const bool passed = std::abs(projection.cols[0].x - expected) < 1.e-4f
        && std::abs(wide.cols[0].x - tall.cols[0].x) > 0.05f;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Camera aspect gate: 16:9 scale=%.4f expected=%.4f (%s)",
        projection.cols[0].x, expected, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

// Frame-ring budget: does the worst case the engine can produce still fit, with
// margin to spare?
//
// The ring is the tightest ceiling in the engine and nothing checked it. This
// models the worst case from the *real* constants rather than a literal, so it
// goes red when any of them moves - raise kMaxInstances, grow FrameConstants,
// add a fourth geometry pass, and the arithmetic stops fitting.
//
// Worst case is driven by *batch* count, not instance count: per-batch constants
// are ~1 KB across shadow + forward + motion while an instance is only 144
// bytes, and batch identity includes index_count and every bound texture, so a
// scene with as many distinct materials as objects gets one batch per instance.
// That is the case batching cannot help, and it is what this budgets for.
//
// Calibration: an earlier version of this gate drove a real 512-batch frame
// through the graph and measured a peak of 600,832 bytes. This model computes
// 601,856 - within 1,024 bytes, one alignment quantum. That measured run is not
// kept because executing the standard frame from inside the gate sequence
// produced 1,023 D3D12 debug-layer errors (CopyDescriptorsSimple reading a
// CPU-write-only heap); the model is the safe way to hold the same line, and
// the divergence is recorded in the ROADMAP as unfinished business.
[[maybe_unused]] bool run_frame_ring_budget_gate(const engine::rhi::IDevice& device) {
    const engine::rhi::FrameRingStats ring = device.frame_ring_stats();

    // alloc_frame_memory rounds every request up to kBufferAlign (256).
    constexpr engine::u64 kAlign = 256;
    auto aligned = [](engine::u64 bytes) {
        return (bytes + kAlign - 1) & ~(kAlign - 1);
    };

    constexpr engine::u64 kBatches = engine::scene::kMaxInstances;
    // One upload for the whole frame, not one per pass.
    const engine::u64 instance_array =
        aligned(kBatches * sizeof(engine::renderer::InstanceData));
    const engine::u64 per_batch =
        aligned(sizeof(engine::renderer::ShadowConstants))
        + aligned(sizeof(engine::renderer::FrameConstants))
        + aligned(sizeof(engine::renderer::motion::Constants));
    // Fixed post-processing cost: sky, 5 bloom downsamples + 4 upsamples, TAA,
    // tonemap, and up to 3 SMAA passes.
    const engine::u64 fixed =
        aligned(sizeof(engine::renderer::sky::Constants))
        + 9 * aligned(sizeof(engine::renderer::bloom::Constants))
        + aligned(sizeof(engine::renderer::taa::Constants))
        + aligned(sizeof(engine::renderer::tonemap::Constants))
        + 3 * aligned(sizeof(engine::renderer::aa::Constants));

    const engine::u64 worst = instance_array + kBatches * per_batch + fixed;
    const engine::f64 used = ring.capacity_bytes > 0
        ? static_cast<engine::f64>(worst) / static_cast<engine::f64>(ring.capacity_bytes)
        : 1.0;
    const engine::f64 headroom = 1.0 - used;
    constexpr engine::f64 kMinHeadroom = 0.15;

    const bool have_capacity = ring.capacity_bytes > 0;
    const bool fits = worst < ring.capacity_bytes;
    // The margin is the point: this goes red *before* the ring can actually run
    // dry, so the next capacity raise fails here instead of silently dropping
    // draws at runtime.
    const bool roomy = headroom >= kMinHeadroom;
    const bool never_dry = ring.exhausted_frames == 0;
    const bool passed = have_capacity && fits && roomy && never_dry;

    char message[256];
    std::snprintf(message, sizeof(message),
        "Frame ring budget gate: batches=%llu per_batch=%llu instances=%llu fixed=%llu "
        "worst=%llu/%llu headroom=%.1f%% (min %.0f%%) exhausted=%llu (%s)",
        static_cast<unsigned long long>(kBatches),
        static_cast<unsigned long long>(per_batch),
        static_cast<unsigned long long>(instance_array),
        static_cast<unsigned long long>(fixed),
        static_cast<unsigned long long>(worst),
        static_cast<unsigned long long>(ring.capacity_bytes),
        headroom * 100.0, kMinHeadroom * 100.0,
        static_cast<unsigned long long>(ring.exhausted_frames),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

[[maybe_unused]] bool run_swap_gate() {
    engine::renderer::RenderGraph probe;
    engine::renderer::StandardFrameDesc desc{};
    desc.log_ready = false;
    const bool compiled = engine::renderer::setup_standard_frame(probe, std::move(desc));
    engine::log(compiled ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render,
        compiled ? "Swap gate: standard frame owned by renderer (pass)"
                 : "Swap gate: standard frame owned by renderer (FAIL)");
    return compiled;
}

} // namespace sandbox
