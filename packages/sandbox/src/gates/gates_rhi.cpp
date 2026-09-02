#include "../sandbox_common.hpp"

// RHI, shader compiler and swapchain gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

bool run_storage_texture_gate(engine::rhi::IDevice& device,
    engine::shaders::IShaderCompiler& compiler, const std::string& shader_path) {
    // RHI #9. Until this landed, a compute pass could be *ordered* against a
    // graph resource but not write one - no Access mapped to the storage state,
    // so PassKind::Compute carried half a feature.
    engine::shaders::ShaderCompileDesc cs_desc{};
    cs_desc.file_path = shader_path;
    cs_desc.entry_point = "cs_main";
    cs_desc.target_profile = "cs_6_0";
    cs_desc.target = engine::shaders::ShaderTarget::Dxil;
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
        "Storage texture gate: created=%s dispatch=%s probes=%u,%u,%u,%u "
        "cross_thread_readback=%s (%s)",
        created ? "yes" : "no", pso ? "yes" : "no", probes[0], probes[1], probes[2], probes[3],
        values_ok ? "yes" : "NO", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}


bool run_msaa_gate(engine::rhi::IDevice& device, engine::shaders::IShaderCompiler& compiler,
    const std::string& shader_path) {
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
        desc.target = engine::shaders::ShaderTarget::Dxil;
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
        "MSAA gate: samples=%u/%u resolved=(blend=%u lit=%u) single=(blend=%u lit=%u) "
        "mismatch_diagnosed=%u (%s)",
        ms_target ? ms_target->sample_count() : 0u, resolved ? resolved->sample_count() : 0u,
        resolved_blend, resolved_lit, reference_blend, reference_lit, mismatch_hits,
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
    const bool dxil = bytecode_is_dxil(first.data);
    const bool passed = second_hit && first.data.size() == second.data.size() && dxil;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Shader cache gate: first=%s second=%s dxil=%s (%s)",
        first_hit ? "hit" : "miss",
        second_hit ? "hit" : "miss",
        dxil ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_dxc_gate(const engine::shaders::ShaderCompileDesc& desc,
    const engine::shaders::ShaderBytecode& bytecode) {
    const bool sm6 = desc.target_profile.find("6_") != std::string::npos;
    const bool dxil = bytecode_is_dxil(bytecode.data);
    const bool target_ok = desc.target == engine::shaders::ShaderTarget::Dxil;
    const bool passed = sm6 && dxil && target_ok && !bytecode.data.empty();
    char message[192];
    std::snprintf(message, sizeof(message),
        "DXC gate: profile=%s target=dxil dxil=%s bytes=%zu (%s)",
        desc.target_profile.c_str(), dxil ? "yes" : "no", bytecode.data.size(),
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

    engine::shaders::ShaderCompileDesc spirv{};
    spirv.file_path = "unused.hlsl";
    spirv.entry_point = "main";
    spirv.target_profile = "vs_6_0";
    spirv.target = engine::shaders::ShaderTarget::Spirv;
    engine::shaders::ShaderBytecode unused;
    std::string error;
    const bool spirv_rejected = !compiler.compile(spirv, unused, error)
        && error.find("SPIR-V") != std::string::npos;
    const bool target_default =
        engine::shaders::ShaderCompileDesc{}.target == engine::shaders::ShaderTarget::Dxil;

    const bool passed = sampler_ok && spirv_rejected && target_default;
    char message[224];
    std::snprintf(message, sizeof(message),
        "RHI contract gate: sampler=%s spirv_rejected=%s target_enum=%s (%s)",
        sampler_ok ? "yes" : "no",
        spirv_rejected ? "yes" : "no", target_default ? "yes" : "no",
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
