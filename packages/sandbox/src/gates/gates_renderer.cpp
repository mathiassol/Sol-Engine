#include "../sandbox_common.hpp"

// Renderer and render graph gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

bool run_two_draw_items_gate() {
    engine::renderer::DrawItem draws[2]{};
    draws[0].model = engine::math::Mat4::identity();
    draws[1].model = engine::math::Mat4::translate({2.f, 0.f, 0.f});
    const bool passed
        = std::memcmp(&draws[0].model, &draws[1].model, sizeof(engine::math::Mat4)) != 0;
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render,
        passed ? "DrawItem pair gate: two models (pass)"
               : "DrawItem pair gate: two models (FAIL)");
    return passed;
}

// The safety net for the whole registry: every entry must have been created.
// A forgotten creation row makes --gates red here instead of producing a frame
// that is quietly missing a pass, which is what the old hand-copied plumbing
// did. Iterating kFramePipelines is why the table exists.
bool run_pipeline_set_gate(const engine::renderer::FramePipelines& pipelines) {
    engine::u32 present = 0;
    const char* first_missing = nullptr;
    for (engine::usize k = 0; k < engine::renderer::kFramePipelineCount; ++k) {
        const auto& entry = engine::renderer::kFramePipelines[k];
        if (pipelines.*(entry.field) != nullptr) {
            present += 1;
        } else if (!first_missing) {
            first_missing = entry.name;
        }
    }
    const bool passed = present == engine::renderer::kFramePipelineCount;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Pipeline set gate: present=%u/%zu missing=%s (%s)",
        present, engine::renderer::kFramePipelineCount,
        first_missing ? first_missing : "none",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_shadow_gate(const engine::scene::World& world,
    const engine::assets::gpu::GpuMeshStore& meshes,
    const engine::rhi::IGraphicsPipeline* shadow_pipeline) {
    const engine::math::Mat4 sun = engine::renderer::make_sun_view_proj(
        world.sun.direction, sandbox::scene_world_bounds(world, meshes));
    const engine::math::Mat4 identity = engine::math::Mat4::identity();
    const bool matrix_ok = std::memcmp(&sun, &identity, sizeof(engine::math::Mat4)) != 0
        && std::isfinite(sun.cols[0].x) && std::abs(sun.cols[0].x) > 0.01f;
    const bool pipeline_ok = shadow_pipeline != nullptr;
    const bool layout_ok = sizeof(engine::renderer::ShadowConstants) == 80;
    const bool passed = matrix_ok && pipeline_ok && layout_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Shadow gate: sun_proj.x=%.3f pipeline=%s map=%u (%s)",
        sun.cols[0].x, pipeline_ok ? "yes" : "no", engine::renderer::kShadowMapSize,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_hdr_gate(const engine::scene::World& world,
    const engine::rhi::IGraphicsPipeline* tonemap_pipeline) {
    // What this needs to prove is that scene radiance genuinely leaves LDR range,
    // so RGBA16 scene_color and the tonemap are doing real work rather than
    // dressing up values that would have fit in 8 bits anyway. Any channel above
    // 1 does that. The previous 3.5/3.0/2.5 floors were not derived from that -
    // they were the pre-sRGB tuning written down as a threshold, and they would
    // now reject any correctly exposed scene.
    const bool sun_hot = world.sun.color.x > 1.f && world.sun.color.y > 1.f
        && world.sun.color.z > 1.f;
    const bool pipeline_ok = tonemap_pipeline != nullptr;
    const bool format_ok = static_cast<engine::u8>(engine::rhi::Format::RGBA16_FLOAT)
        != static_cast<engine::u8>(engine::rhi::Format::RGBA8_UNORM);
    const bool passed = sun_hot && pipeline_ok && format_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "HDR gate: sun=(%.2f,%.2f,%.2f) tonemap=%s rgba16=%s (%s)",
        world.sun.color.x, world.sun.color.y, world.sun.color.z,
        pipeline_ok ? "yes" : "no", format_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

[[maybe_unused]] bool run_async_compile_gate(engine::shaders::IShaderHotReloader& watcher) {
    engine::Clock clock;
    watcher.request_compile();

    // The claim under test is that poll() never blocks the caller waiting on
    // the compiler - it hands back Busy and returns.
    //
    // Asserting that on max poll time alone made this gate load-sensitive: the
    // measurement is wall-clock around a call, so one OS deschedule under a
    // loaded machine reads as a 30 ms "block" from a call that did no such
    // thing. Observed 0.36-0.55 ms idle and 33.60 ms while two full builds ran
    // beside it, which is a 60x outlier against a 16 ms bar.
    //
    // So judge the *proportion* of slow polls, not a count of them. If poll
    // really blocked on the compile then every poll before it finishes is
    // slow; a scheduling artifact is a minority of samples.
    //
    // A fixed allowance does not work here, and the negative control proves
    // it: simulating a blocking poll produced slow=2/2 - literally every poll
    // - yet an "allow 2" bar passed it, because a blocking poll also makes the
    // loop run few iterations. Majority-slow is the test that separates the
    // two.
    constexpr engine::f32 kMaxPollMs = 16.f;
    constexpr engine::f64 kTimeoutS = 30.0;
    engine::f32 first_poll_ms = 0.f;
    engine::f32 max_poll_ms = 0.f;
    engine::u32 poll_count = 0;
    engine::u32 slow_polls = 0;
    bool have_first = false;
    bool saw_busy = false;
    bool reloaded = false;
    const engine::f64 start = clock.now();

    while (clock.now() - start < kTimeoutS) {
        engine::shaders::ShaderBytecode vs;
        engine::shaders::ShaderBytecode ps;
        std::string error;
        const engine::f64 t0 = clock.now();
        const auto status = watcher.poll(vs, ps, error);
        const engine::f32 poll_ms = static_cast<engine::f32>((clock.now() - t0) * 1000.0);
        if (!have_first) {
            first_poll_ms = poll_ms;
            have_first = true;
        }
        ++poll_count;
        if (poll_ms > max_poll_ms) {
            max_poll_ms = poll_ms;
        }
        if (poll_ms > kMaxPollMs) {
            ++slow_polls;
        }
        if (status == engine::shaders::ShaderReloadStatus::Busy) {
            saw_busy = true;
        }
        if (status == engine::shaders::ShaderReloadStatus::Reloaded
            && !vs.data.empty() && !ps.data.empty()) {
            reloaded = true;
            break;
        }
        if (status == engine::shaders::ShaderReloadStatus::Failed) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Majority-slow means blocking. Needs at least two samples to judge.
    const bool fast = have_first && poll_count >= 2 && slow_polls * 2 <= poll_count;
    const bool passed = reloaded && fast;
    char message[256];
    std::snprintf(message, sizeof(message),
        "Async compile gate: first_poll=%.2fms max_poll=%.2fms slow=%u/%u "
        "(>%.0fms, fail if majority) "
        "busy=%s reloaded=%s (%s)",
        first_poll_ms, max_poll_ms, slow_polls, poll_count,
        static_cast<double>(kMaxPollMs),
        saw_busy ? "yes" : "no", reloaded ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_frustum_gate(const engine::scene::World& world, const FlyCamera& camera,
    const sandbox::WorldExtractAssets& assets) {
    engine::scene::World copy = world;
    copy.camera.view = camera.view();
    copy.camera.projection = camera.projection(16.f / 9.f);
    engine::Arena arena(256 * 1024);
    engine::renderer::RenderSnapshot snapshot{};
    snapshot.width = 1280;
    snapshot.height = 720;
    const auto stats = sandbox::extract_world(copy, camera.position, assets, false, nullptr, arena,
        snapshot);
    const engine::u32 skipped = stats.considered - stats.visible;
    // Batches must collapse the drawn set, never exceed it. Reported here
    // because this is the one gate that runs the real demo world, so the
    // numbers describe actual content rather than a synthetic scene.
    const bool batched = stats.batches > 0 && stats.batches <= stats.drawn;
    const bool passed = world.instance_count >= 64 && stats.visible >= 5 && skipped >= 16
        && batched;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Frustum gate: instances=%u visible=%u skipped=%u drawn=%u batches=%u (%s)",
        world.instance_count, stats.visible, skipped, stats.drawn, stats.batches,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_material_gate(const engine::scene::World& world, const FlyCamera& camera,
    const sandbox::WorldExtractAssets& assets, engine::f32 gltf_metallic,
    engine::f32 gltf_roughness) {
    const bool table_ok = world.material_count >= 2;
    bool handles_ok = world.instance_count > 0;
    for (engine::u32 i = 0; i < world.instance_count; ++i) {
        if (world.instances[i].material >= world.material_count) {
            handles_ok = false;
        }
    }

    engine::scene::World copy = world;
    copy.camera.view = camera.view();
    copy.camera.projection = camera.projection(16.f / 9.f);
    engine::Arena arena(256 * 1024);
    engine::renderer::RenderSnapshot before{};
    before.width = 1280;
    before.height = 720;
    sandbox::extract_world(copy, camera.position, assets, false, nullptr, arena, before);

    constexpr engine::f32 kProbeRoughness = 0.03125f;
    engine::scene::World mutated = copy;
    for (engine::u32 i = 0; i < mutated.material_count; ++i) {
        mutated.materials[i].roughness = kProbeRoughness;
    }
    engine::Arena arena_mutated(256 * 1024);
    engine::renderer::RenderSnapshot after{};
    after.width = 1280;
    after.height = 720;
    sandbox::extract_world(mutated, camera.position, assets, false, nullptr, arena_mutated, after);

    bool draws_ok = !after.draws.empty();
    for (const engine::renderer::DrawItem& draw : after.draws) {
        if (draw.roughness != kProbeRoughness) {
            draws_ok = false;
        }
    }
    const bool changed = !before.draws.empty() && !after.draws.empty()
        && before.draws[0].roughness != after.draws[0].roughness;
    const bool layout_ok = sizeof(engine::renderer::FrameConstants) == 336;
    const bool gltf_ok = gltf_metallic >= 0.f && gltf_metallic <= 1.f
        && gltf_roughness >= 0.f && gltf_roughness <= 1.f;
    const bool passed = table_ok && handles_ok && draws_ok && changed && layout_ok && gltf_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Material gate: materials=%u handles=%s roughness_is_data=%s layout=%s (%s)",
        world.material_count, handles_ok ? "yes" : "no",
        (draws_ok && changed) ? "yes" : "no", layout_ok ? "400" : "bad",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_pbr_gate() {
    using engine::renderer::pbr::d_ggx;
    using engine::renderer::pbr::evaluate_punctual;
    using engine::renderer::pbr::f0_from_metal;
    using engine::renderer::pbr::f_schlick;
    using engine::renderer::pbr::perceptual_to_alpha;

    const engine::f32 alpha_smooth = perceptual_to_alpha(0.2f);
    const engine::f32 alpha_rough = perceptual_to_alpha(0.8f);
    const bool ggx_peak = d_ggx(1.f, alpha_smooth) > d_ggx(1.f, alpha_rough) * 8.f;

    const engine::math::Vec3 red{0.8f, 0.12f, 0.04f};
    const engine::math::Vec3 metal_f0 = f0_from_metal(red, 1.f);
    const bool metal_f0_ok = metal_f0.x > 0.7f && metal_f0.y < 0.2f && metal_f0.z < 0.1f;

    const engine::math::Vec3 n{0.f, 0.f, 1.f};
    const engine::math::Vec3 white{1.f, 1.f, 1.f};
    const engine::math::Vec3 metal_lo = evaluate_punctual(n, n, n, red, 1.f, 0.25f, white);
    const engine::math::Vec3 diel_lo = evaluate_punctual(n, n, n, red, 0.f, 0.25f, white);
    const bool metal_has_spec = metal_lo.x > 0.05f;
    const bool dielectric_lit = diel_lo.x > 0.05f && diel_lo.y > 0.01f;

    const engine::math::Vec3 f_grazing = f_schlick(0.f, {0.04f, 0.04f, 0.04f});
    const engine::math::Vec3 f_normal = f_schlick(1.f, {0.04f, 0.04f, 0.04f});
    const bool fresnel_ok = f_grazing.x > 0.95f && f_normal.x < 0.06f;
    const bool alpha_ok = perceptual_to_alpha(0.5f) == 0.25f;

    const bool passed = ggx_peak && metal_f0_ok && metal_has_spec && dielectric_lit && fresnel_ok
        && alpha_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "PBR gate: ggx_peak=%s metal_f0=%s points_spec=yes energy=kd_1-F alpha=r^2 (%s)",
        ggx_peak ? "yes" : "no", metal_f0_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_ibl_gate(const ForwardDemo& demo) {
    using engine::renderer::ibl::integrate_brdf;
    using engine::renderer::ibl::kIntensity;
    using engine::renderer::ibl::kMaxLod;
    using engine::renderer::ibl::lod_from_roughness;
    using engine::renderer::ibl::sky_radiance;
    using engine::renderer::pbr::kDielectricF0;

    const bool intensity_ok = kIntensity == 1.f;
    const bool f0_ok = kDielectricF0 == 0.04f;
    const bool lod_ok = lod_from_roughness(0.f) == 0.f && lod_from_roughness(1.f) == kMaxLod;

    const engine::math::Vec2 dfg = integrate_brdf(0.95f, 0.08f, 64);
    const bool lut_ok = dfg.x > dfg.y * 4.f && dfg.x > 0.4f;

    const engine::math::Vec3 zenith = sky_radiance({0.f, 1.f, 0.f});
    const engine::math::Vec3 ground = sky_radiance({0.f, -1.f, 0.f});
    const bool sky_ok = zenith.y > ground.y * 1.5f;

    const bool cubes_ok = demo.ibl_irradiance
        && demo.ibl_irradiance->dimension() == engine::rhi::TextureDimension::Cube
        && demo.ibl_prefilter
        && demo.ibl_prefilter->dimension() == engine::rhi::TextureDimension::Cube
        && demo.ibl_prefilter->mip_levels() == engine::renderer::ibl::kPrefilterMips
        && demo.ibl_brdf_lut
        && demo.ibl_brdf_lut->width() == engine::renderer::ibl::kLutSize;

    const bool passed = intensity_ok && f0_ok && lod_ok && lut_ok && sky_ok && cubes_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "IBL gate: intensity=1 split_sum=yes lut=%s cubes=%s (%s)",
        lut_ok ? "yes" : "no", cubes_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_sky_gate(const ForwardDemo& demo) {
    using engine::renderer::sky::apply_sun_disk;
    using engine::renderer::sky::direction_from_ndc;
    using engine::renderer::sky::kCubemapSize;
    using engine::renderer::sky::kIntensity;
    using engine::renderer::sky::kMode;
    using engine::renderer::sky::kSunDisk;
    using engine::renderer::sky::make_constants;
    using engine::renderer::sky::Mode;
    using engine::renderer::sky::radiance;

    const bool mode_ok = kMode == Mode::Cubemap;
    const bool intensity_ok = kIntensity == 1.f;
    const bool sun_ok = kSunDisk;

    const engine::math::Vec3 zenith = radiance({0.f, 1.f, 0.f});
    const engine::math::Vec3 ground = radiance({0.f, -1.f, 0.f});
    const bool gradient_ok = zenith.y > ground.y * 1.5f && zenith.z > zenith.x
        && zenith.x == engine::renderer::ibl::sky_radiance({0.f, 1.f, 0.f}).x;

    const engine::math::Vec3 sun_dir = demo.world.sun.direction.normalized();
    const engine::math::Vec3 sun_col = demo.world.sun.color;
    const engine::math::Vec3 ibl_on_sun = engine::renderer::ibl::sky_radiance(sun_dir);
    const engine::math::Vec3 ibl_off = engine::renderer::ibl::sky_radiance(
        engine::math::Vec3{sun_dir.x + 0.2f, sun_dir.y, sun_dir.z}.normalized());
    const bool ibl_no_disk = std::abs(ibl_on_sun.x - ibl_off.x) < 0.25f;
    const engine::math::Vec3 skybox = apply_sun_disk(sun_dir, ibl_on_sun, sun_dir, sun_col);
    const bool disk_ok = skybox.x > ibl_on_sun.x + 8.f;

    const engine::math::Mat4 view = engine::math::Mat4::look_at(
        {0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 0.f});
    const engine::math::Mat4 projection = engine::math::Mat4::perspective(
        engine::math::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    const auto constants = make_constants(view, projection, sun_dir, sun_col);
    const engine::math::Vec3 forward = direction_from_ndc({0.f, 0.f}, constants);
    const engine::math::Vec3 up = direction_from_ndc({0.f, 1.f}, constants);
    const bool ray_ok = forward.z > 0.9f && std::abs(forward.x) < 0.05f && up.y > forward.y;

    const bool cube_ok = demo.pipelines.sky && demo.sky_cubemap
        && demo.sky_cubemap->dimension() == engine::rhi::TextureDimension::Cube
        && demo.sky_cubemap->mip_levels() == 1
        && demo.sky_cubemap->width() == kCubemapSize
        && demo.sky_cubemap.get() != demo.ibl_prefilter.get()
        && demo.sky_cubemap.get() != demo.ibl_irradiance.get();

    const bool passed = mode_ok && intensity_ok && sun_ok && gradient_ok && ibl_no_disk && disk_ok
        && ray_ok && cube_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Sky gate: cubemap=yes source_not_ggx=yes intensity=1 sun_disk=skybox (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_bloom_gate(const ForwardDemo& demo) {
    using engine::renderer::bloom::apply_knee;
    using engine::renderer::bloom::karis_box;
    using engine::renderer::bloom::kCompositeBeforeTonemap;
    using engine::renderer::bloom::kIntensity;
    using engine::renderer::bloom::kMips;
    using engine::renderer::bloom::kSoftKnee;
    using engine::renderer::bloom::kThreshold;
    using engine::renderer::bloom::make_downsample_constants;
    using engine::renderer::bloom::threshold_params;

    const engine::math::Vec3 dim{0.5f, 0.5f, 0.5f};
    const engine::math::Vec3 hot{2.f, 2.f, 2.f};
    const engine::math::Vec3 below = apply_knee(dim);
    const engine::math::Vec3 above = apply_knee(hot);
    const bool knee_ok = kThreshold == 1.f && kSoftKnee == 0.5f
        && below.x < 0.01f && above.x > 0.4f;

    const engine::math::Vec3 firefly{100.f, 100.f, 100.f};
    const engine::math::Vec3 rest{1.f, 1.f, 1.f};
    const engine::math::Vec3 karis = karis_box(firefly, rest, rest, rest);
    const engine::f32 arith = (100.f + 1.f + 1.f + 1.f) / 4.f;
    const bool karis_ok = karis.x < 5.f && arith > 20.f;

    const auto first = make_downsample_constants(128, 128, true);
    const auto later = make_downsample_constants(64, 64, false);
    const engine::math::Vec4 packed = threshold_params();
    const bool mode_ok = first.params.x > 0.5f && later.params.x < 0.5f
        && packed.x == kThreshold && packed.z > packed.y;

    const bool intensity_ok = kCompositeBeforeTonemap && kIntensity > 0.f && kIntensity <= 0.15f
        && kMips == 5;
    const bool pipeline_ok = demo.pipelines.bloom_downsample && demo.pipelines.bloom_upsample
        && demo.pipelines.tonemap;

    const bool passed = knee_ok && karis_ok && mode_ok && intensity_ok && pipeline_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Bloom gate: karis=yes knee=%.1f intensity=hdr_add mips=%u (%s)",
        kSoftKnee, kMips, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_aa_gate(const ForwardDemo& demo) {
    using engine::renderer::aa::effective_mode;
    using engine::renderer::aa::kAfterTonemap;
    using engine::renderer::aa::kTaaBeforeTonemap;
    using engine::renderer::aa::kDefault;
    using engine::renderer::aa::kSmaaMaxSearch;
    using engine::renderer::aa::kSmaaThreshold;
    using engine::renderer::aa::kStackSpatial;
    using engine::renderer::aa::luma;
    using engine::renderer::aa::Mode;
    using engine::renderer::aa::next_mode;
    using engine::renderer::aa::parse_mode;

    const bool policy_ok = kDefault == Mode::Off && kAfterTonemap && kTaaBeforeTonemap
        && !kStackSpatial && kSmaaThreshold == 0.1f && kSmaaMaxSearch == 8
        && next_mode(Mode::Off) == Mode::Fxaa && next_mode(Mode::Fxaa) == Mode::Smaa
        && next_mode(Mode::Smaa) == Mode::Taa && next_mode(Mode::Taa) == Mode::Off;

    const engine::f32 edge = luma({1.f, 1.f, 1.f}) - luma({0.f, 0.f, 0.f});
    const bool luma_ok = edge > 0.9f && luma({0.2f, 0.2f, 0.2f}) < 0.3f;

    const bool fxaa_ok = demo.pipelines.fxaa != nullptr;
    const bool smaa_ok = demo.pipelines.smaa_edge && demo.pipelines.smaa_weights
        && demo.pipelines.smaa_blend;
    const bool taa_ok = demo.pipelines.taa && demo.pipelines.tonemap_aces;
    const bool exclusive_ok = effective_mode(Mode::Taa, fxaa_ok, smaa_ok, taa_ok) == Mode::Taa
        && effective_mode(Mode::Taa, fxaa_ok, smaa_ok, false) == Mode::Smaa
        && effective_mode(Mode::Smaa, fxaa_ok, smaa_ok, taa_ok) == Mode::Smaa
        && effective_mode(Mode::Fxaa, fxaa_ok, smaa_ok, taa_ok) == Mode::Fxaa
        && effective_mode(Mode::Off, fxaa_ok, smaa_ok, taa_ok) == Mode::Off
        && effective_mode(Mode::Smaa, fxaa_ok, false, false) == Mode::Off;

    Mode parsed = Mode::Taa;
    const bool parse_ok = parse_mode("off", parsed) && parsed == Mode::Off
        && parse_mode("fxaa", parsed) && parsed == Mode::Fxaa
        && parse_mode("smaa", parsed) && parsed == Mode::Smaa
        && parse_mode("taa", parsed) && parsed == Mode::Taa
        && !parse_mode("FXAA", parsed) && !parse_mode("nope", parsed);

    // Two knobs can move the startup mode now - r.aa directly and r.quality as a
    // preset - so asserting the factory default unconditionally goes red for
    // anyone running with either set. Ask the same resolver the setup path used
    // rather than re-deriving the expectation here, which is exactly how this
    // gate broke when r.quality arrived. `false`: the setup path already warned
    // about any bad knob, and a gate must not double-report it.
    const Mode wanted = resolve_quality_from_cvars(false).aa;
    const bool startup_ok = demo.aa_mode == wanted;

    const bool passed = policy_ok && luma_ok && fxaa_ok && smaa_ok && exclusive_ok
        && parse_ok && startup_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "AA gate: startup=%s wanted=%s factory=%s exclusive=yes after_tonemap=yes fxaa=yes (%s)",
        mode_name(demo.aa_mode), mode_name(wanted), mode_name(kDefault),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_taa_gate(const ForwardDemo& demo) {
    using engine::renderer::aa::Mode;
    using engine::renderer::aa::effective_mode;
    using engine::renderer::taa::apply_jitter;
    using engine::renderer::taa::clip_aabb;
    using engine::renderer::taa::jitter_ndc;
    using engine::renderer::taa::jitter_to_uv;
    using engine::renderer::taa::kBeforeTonemap;
    using engine::renderer::taa::kHistoryWeight;
    using engine::renderer::taa::kSampleCount;
    using engine::renderer::taa::radical_inverse;
    using engine::renderer::taa::resolve_sample;
    using engine::renderer::taa::rgb_to_ycocg;
    using engine::renderer::taa::ycocg_to_rgb;

    const engine::math::Vec2 j = jitter_ndc(1, 1280, 720);
    const engine::math::Vec2 j_uv = jitter_to_uv(j);
    const bool jitter_ok = kSampleCount == 8 && radical_inverse(2, 1) > 0.4f
        && radical_inverse(2, 1) < 0.6f && (j.x != 0.f || j.y != 0.f)
        && kHistoryWeight > 0.94f && kHistoryWeight < 0.96f
        && std::abs(j_uv.x - j.x * 0.5f) < 1e-8f
        && std::abs(j_uv.y - j.y * -0.5f) < 1e-8f;

    const engine::math::Mat4 proj = engine::math::Mat4::perspective(
        engine::math::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    const engine::math::Mat4 jittered = apply_jitter(proj, j);
    const engine::math::Mat4 none = apply_jitter(proj, {0.f, 0.f});
    const bool apply_ok = std::abs(jittered.cols[2].x - proj.cols[2].x) > 1e-8f
        && std::abs(none.cols[0].x - proj.cols[0].x) < 1e-6f
        && sizeof(engine::renderer::FrameConstants) == 336
        && sizeof(engine::renderer::taa::Constants) == 48
        && std::abs(engine::renderer::taa::make_constants(1280, 720, j, false).jitter.x - j_uv.x)
            < 1e-8f;

    const engine::math::Vec3 rgb{0.8f, 0.4f, 0.2f};
    const engine::math::Vec3 roundtrip = ycocg_to_rgb(rgb_to_ycocg(rgb));
    const bool ycc_ok = std::abs(roundtrip.x - rgb.x) < 1e-5f
        && std::abs(roundtrip.y - rgb.y) < 1e-5f && std::abs(roundtrip.z - rgb.z) < 1e-5f;

    const engine::math::Vec3 inside
        = clip_aabb({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, {0.2f, 0.3f, 0.4f});
    const engine::math::Vec3 outside
        = clip_aabb({0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, {4.f, 0.5f, 0.5f});
    const bool clip_ok = std::abs(inside.x - 0.2f) < 1e-5f && outside.x <= 1.0001f
        && outside.x >= 0.f;
    const engine::math::Vec3 resolved = resolve_sample({0.1f, 0.1f, 0.1f}, {8.f, 0.1f, 0.1f},
        rgb_to_ycocg({0.f, 0.f, 0.f}), rgb_to_ycocg({0.2f, 0.2f, 0.2f}), kHistoryWeight, false);
    const bool blend_ok = resolved.x < 1.f;
    const engine::math::Vec3 reset = resolve_sample({0.5f, 0.5f, 0.5f}, {8.f, 8.f, 8.f},
        {0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, kHistoryWeight, true);
    const bool reset_ok = std::abs(reset.x - 0.5f) < 1e-5f;

    engine::renderer::RenderGraph probe;
    engine::renderer::StandardFrameDesc frame{};
    frame.log_ready = false;
    const bool compiled = engine::renderer::setup_standard_frame(probe, std::move(frame));
    int bloom_i = -1;
    int taa_i = -1;
    int tonemap_taa_i = -1;
    int tonemap_i = -1;
    for (engine::u32 i = 0; i < probe.pass_count(); ++i) {
        const std::string_view name = probe.pass_name(i);
        if (name == "bloom_up0") {
            bloom_i = static_cast<int>(i);
        } else if (name == "taa_even" && taa_i < 0) {
            taa_i = static_cast<int>(i);
        } else if (name == "tonemap_taa_even" && tonemap_taa_i < 0) {
            tonemap_taa_i = static_cast<int>(i);
        } else if (name == "tonemap") {
            tonemap_i = static_cast<int>(i);
        }
    }
    const bool pass_ok = compiled && bloom_i >= 0 && taa_i > bloom_i && tonemap_taa_i > taa_i
        && tonemap_i > tonemap_taa_i
        && probe.find_resource("taa_history_a").valid()
        && probe.find_resource("taa_history_b").valid();

    const bool hdr_ok = kBeforeTonemap && engine::renderer::aa::kTaaBeforeTonemap
        && engine::renderer::aa::kDefault == Mode::Off
        && demo.pipelines.taa != nullptr && demo.pipelines.tonemap_aces != nullptr
        && effective_mode(Mode::Taa, true, true, true) == Mode::Taa
        && effective_mode(Mode::Smaa, true, true, true) == Mode::Smaa;

    const bool passed = jitter_ok && apply_ok && ycc_ok && clip_ok && blend_ok && reset_ok
        && pass_ok && hdr_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "TAA gate: default=off optional=yes hdr=yes jitter=%s clip=ycocg pass=%s (%s)",
        jitter_ok && apply_ok ? "yes" : "no", pass_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

// scene::kMaxInstances is not just an array bound - it is coupled to
// motion::kHistorySlots, to the frame arena, and (through the extract path) to
// the GPU per-frame budgets. Those couplings are invisible: exceeding
// kHistorySlots does not crash or warn, it silently hands every instance past
// the limit prev_model == model, which reads as "this object did not move" and
// makes TAA reproject it wrongly.
//
// This gate fills the scene to capacity and checks the whole range survives
// extract, twice, with movement in between.
bool run_instance_capacity_gate() {
    using engine::renderer::motion::MotionHistory;

    constexpr engine::u32 kCount = engine::scene::kMaxInstances;
    char dummy{};
    std::vector<engine::renderer::ExtractInstance> instances(kCount);
    for (engine::u32 i = 0; i < kCount; ++i) {
        auto& inst = instances[i];
        inst.pipeline = reinterpret_cast<engine::rhi::IGraphicsPipeline*>(&dummy);
        inst.vertex_buffer = reinterpret_cast<engine::rhi::IBuffer*>(&dummy);
        inst.index_buffer = reinterpret_cast<engine::rhi::IBuffer*>(&dummy);
        inst.texture = reinterpret_cast<engine::rhi::ITexture*>(&dummy);
        // Empty local_bounds: Frustum::intersects is conservative for an
        // invalid box, so every instance is visible and `drawn` is exact.
        inst.model = engine::math::Mat4::translate(
            {static_cast<engine::f32>(i) * 0.01f, 0.f, 0.f});
        inst.id = i;
        inst.index_count = 3;
        inst.vertex_stride = 32;
    }

    const engine::math::Mat4 view = engine::math::Mat4::look_at(
        {0.f, 0.f, -8.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    const engine::math::Mat4 proj =
        engine::math::Mat4::perspective(engine::math::radians(60.f), 16.f / 9.f, 0.1f, 500.f);

    MotionHistory history{};
    engine::renderer::ExtractDesc desc{};
    desc.view = view;
    desc.projection = proj;
    desc.history = &history;
    desc.instances = {instances.data(), instances.size()};

    // DrawItem is 192 bytes; size the arena for the whole scene with headroom
    // so an arena overflow cannot be mistaken for a cull.
    engine::Arena arena(static_cast<engine::usize>(kCount) * 512 + 64 * 1024);
    engine::renderer::RenderSnapshot first{};
    const auto stats_first = engine::renderer::extract_visible(desc, arena, first);
    const bool all_drawn = stats_first.drawn == kCount && first.draws.size() == kCount
        && !arena.overflowed();

    // Move every instance, extract again. Each one's prev_model must now be
    // its *previous* transform. An instance past kHistorySlots gets
    // prev_model == model instead, which is exactly the silent failure.
    for (engine::u32 i = 0; i < kCount; ++i) {
        instances[i].model = engine::math::Mat4::translate(
            {static_cast<engine::f32>(i) * 0.01f, 1.f, 0.f});
    }
    engine::Arena arena2(static_cast<engine::usize>(kCount) * 512 + 64 * 1024);
    engine::renderer::RenderSnapshot second{};
    engine::renderer::extract_visible(desc, arena2, second);

    engine::u32 tracked = 0;
    if (second.draws.size() == kCount) {
        for (engine::u32 i = 0; i < kCount; ++i) {
            // prev_model.y differs from model.y only if history covered slot i.
            if (second.draws[i].prev_model.cols[3].y != second.draws[i].model.cols[3].y) {
                ++tracked;
            }
        }
    }
    const bool history_ok = tracked == kCount;

    const bool passed = all_drawn && history_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Instance capacity gate: cap=%u drawn=%u history_tracked=%u/%u slots=%u "
        "arena_overflow=%s (%s)",
        kCount, stats_first.drawn, tracked, kCount,
        engine::renderer::motion::kHistorySlots, arena.overflowed() ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

// Batching is only correct if three things line up: runs are grouped by
// everything the pipeline binds per draw, every drawn instance appears exactly
// once, and instances[batch.first_instance + k] is the k-th instance of that
// batch - because that last expression is literally what the vertex shader
// evaluates as sol_instances[instance_base.x + SV_InstanceID].
bool run_instancing_gate() {
    char mesh_a{}, mesh_b{}, tex_a{}, tex_b{}, pipe{};
    auto make = [&](void* mesh, void* tex, engine::u32 id, engine::f32 x) {
        engine::renderer::ExtractInstance inst{};
        inst.pipeline = reinterpret_cast<engine::rhi::IGraphicsPipeline*>(&pipe);
        inst.vertex_buffer = reinterpret_cast<engine::rhi::IBuffer*>(mesh);
        inst.index_buffer = reinterpret_cast<engine::rhi::IBuffer*>(mesh);
        inst.texture = reinterpret_cast<engine::rhi::ITexture*>(tex);
        inst.model = engine::math::Mat4::translate({x, 0.f, 0.f});
        inst.id = id;
        inst.index_count = 3;
        inst.vertex_stride = 32;
        inst.metallic = x;              // distinct per instance, so a wrong
        inst.roughness = x + 100.f;     // index shows up as wrong material
        return inst;
    };

    // Three runs: 3x(mesh_a,tex_a), 2x(mesh_a,tex_b), 2x(mesh_b,tex_a).
    // The middle run shares a mesh with its neighbours and differs only by
    // texture - which must still split the batch, because the texture is
    // bound per draw.
    std::vector<engine::renderer::ExtractInstance> instances;
    for (engine::u32 i = 0; i < 3; ++i) instances.push_back(make(&mesh_a, &tex_a, i, 1.f + i));
    for (engine::u32 i = 0; i < 2; ++i) instances.push_back(make(&mesh_a, &tex_b, 3 + i, 10.f + i));
    for (engine::u32 i = 0; i < 2; ++i) instances.push_back(make(&mesh_b, &tex_a, 5 + i, 20.f + i));

    engine::renderer::motion::MotionHistory history{};
    engine::renderer::ExtractDesc desc{};
    desc.view = engine::math::Mat4::look_at({0.f, 0.f, -50.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    desc.projection =
        engine::math::Mat4::perspective(engine::math::radians(90.f), 16.f / 9.f, 0.1f, 500.f);
    desc.history = &history;
    desc.instances = {instances.data(), instances.size()};

    engine::Arena arena(256 * 1024);
    engine::renderer::RenderSnapshot snap{};
    const auto stats = engine::renderer::extract_visible(desc, arena, snap);

    const bool grouped = snap.batches.size() == 3 && stats.batches == 3
        && snap.batches[0].instance_count == 3
        && snap.batches[1].instance_count == 2
        && snap.batches[2].instance_count == 2;

    // Texture alone must split a batch even when the mesh matches.
    const bool split_on_texture = snap.batches.size() >= 2
        && snap.batches[0].vertex_buffer == snap.batches[1].vertex_buffer
        && snap.batches[0].texture != snap.batches[1].texture;

    // Every drawn instance covered exactly once, no gaps and no overlap.
    engine::u32 covered = 0;
    bool contiguous = true;
    for (const auto& batch : snap.batches) {
        contiguous = contiguous && batch.first_instance == covered;
        covered += batch.instance_count;
    }
    const bool coverage_ok = contiguous && covered == stats.drawn
        && snap.instances.size() == stats.drawn;

    // The shader evaluates sol_instances[instance_base.x + SV_InstanceID].
    // Grouping permutes instances relative to `draws`, so the check is that
    // every instance a batch will read came from a draw with *that batch's
    // key*, and that the draws are covered exactly once - no loss, no
    // duplication, nothing landing under the wrong texture.
    std::vector<bool> used(snap.draws.size(), false);
    bool mapping_ok = coverage_ok;
    for (const auto& batch : snap.batches) {
        for (engine::u32 k = 0; k < batch.instance_count && mapping_ok; ++k) {
            const auto& inst = snap.instances[batch.first_instance + k];
            bool matched = false;
            for (engine::usize d = 0; d < snap.draws.size(); ++d) {
                const auto& draw = snap.draws[d];
                if (used[d] || draw.texture != batch.texture
                    || draw.vertex_buffer != batch.vertex_buffer) {
                    continue;
                }
                if (inst.model.cols[3].x == draw.model.cols[3].x
                    && inst.material_params.x == draw.metallic
                    && inst.material_params.y == draw.roughness) {
                    used[d] = true;
                    matched = true;
                    break;
                }
            }
            mapping_ok = matched;
        }
    }
    for (bool u : used) {
        mapping_ok = mapping_ok && u;
    }

    const bool passed = grouped && split_on_texture && coverage_ok && mapping_ok;
    char message[256];
    std::snprintf(message, sizeof(message),
        "Instancing gate: drawn=%u batches=%u sizes=%zu/%zu/%zu split_on_texture=%s "
        "coverage=%s shader_mapping=%s (%s)",
        stats.drawn, stats.batches,
        snap.batches.size() > 0 ? static_cast<size_t>(snap.batches[0].instance_count) : 0,
        snap.batches.size() > 1 ? static_cast<size_t>(snap.batches[1].instance_count) : 0,
        snap.batches.size() > 2 ? static_cast<size_t>(snap.batches[2].instance_count) : 0,
        split_on_texture ? "yes" : "no", coverage_ok ? "yes" : "no",
        mapping_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_motion_gate(const ForwardDemo& demo) {
    using engine::renderer::motion::Constants;
    using engine::renderer::motion::kFormat;
    using engine::renderer::motion::MotionHistory;
    using engine::renderer::motion::nearly_zero;
    using engine::renderer::motion::screen_uv_motion;

    const engine::math::Mat4 view_a = engine::math::Mat4::look_at(
        {0.f, 0.f, -4.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    const engine::math::Mat4 view_b = engine::math::Mat4::look_at(
        {0.4f, 0.f, -4.f}, {0.4f, 0.f, 0.f}, {0.f, 1.f, 0.f});
    const engine::math::Mat4 proj =
        engine::math::Mat4::perspective(engine::math::radians(60.f), 16.f / 9.f, 0.1f, 100.f);
    const engine::math::Mat4 model = engine::math::Mat4::identity();
    const engine::math::Mat4 moved = engine::math::Mat4::translate({0.5f, 0.f, 0.f});
    const engine::math::Vec3 local{0.3f, 0.15f, 0.f};
    const engine::math::Mat4 vp_a = proj * view_a;
    const engine::math::Mat4 vp_b = proj * view_b;

    const bool static_ok = nearly_zero(screen_uv_motion(vp_a, model, vp_a, model, local));
    const engine::math::Vec2 camera = screen_uv_motion(vp_b, model, vp_a, model, local);
    const bool camera_ok = !nearly_zero(camera);
    const engine::math::Vec2 object = screen_uv_motion(vp_a, moved, vp_a, model, local);
    const bool object_ok = !nearly_zero(object);
    const bool uv_ok = static_ok && sizeof(engine::renderer::FrameConstants) == 336
        && sizeof(Constants) == 160 && kFormat == engine::rhi::Format::RGBA16_FLOAT;

    char dummy{};
    engine::renderer::ExtractInstance inst{};
    inst.pipeline = reinterpret_cast<engine::rhi::IGraphicsPipeline*>(&dummy);
    inst.vertex_buffer = reinterpret_cast<engine::rhi::IBuffer*>(&dummy);
    inst.index_buffer = reinterpret_cast<engine::rhi::IBuffer*>(&dummy);
    inst.texture = reinterpret_cast<engine::rhi::ITexture*>(&dummy);
    inst.model = model;
    inst.id = 0;
    inst.index_count = 3;
    inst.vertex_stride = 32;

    MotionHistory history{};
    engine::renderer::ExtractDesc desc{};
    desc.view = view_a;
    desc.projection = proj;
    desc.history = &history;
    desc.instances = {&inst, 1};

    engine::Arena first_arena(64 * 1024);
    engine::renderer::RenderSnapshot first{};
    engine::renderer::extract_visible(desc, first_arena, first);
    const bool first_zero = first.draws.size() == 1
        && nearly_zero(screen_uv_motion(first.projection * first.view, first.draws[0].model,
            first.prev_view_proj, first.draws[0].prev_model, local));

    desc.view = view_a;
    engine::Arena still_arena(64 * 1024);
    engine::renderer::RenderSnapshot still{};
    engine::renderer::extract_visible(desc, still_arena, still);
    const bool still_zero = still.draws.size() == 1
        && nearly_zero(screen_uv_motion(still.projection * still.view, still.draws[0].model,
            still.prev_view_proj, still.draws[0].prev_model, local));

    desc.view = view_b;
    engine::Arena next_arena(64 * 1024);
    engine::renderer::RenderSnapshot next{};
    engine::renderer::extract_visible(desc, next_arena, next);
    const engine::math::Vec2 hist_cam = next.draws.size() == 1
        ? screen_uv_motion(next.projection * next.view, next.draws[0].model, next.prev_view_proj,
            next.draws[0].prev_model, local)
        : engine::math::Vec2{};
    const bool history_ok = first_zero && still_zero && !nearly_zero(hist_cam)
        && std::abs(hist_cam.x - camera.x) < 1e-4f && std::abs(hist_cam.y - camera.y) < 1e-4f;

    engine::renderer::RenderGraph probe;
    engine::renderer::StandardFrameDesc frame{};
    frame.log_ready = false;
    const bool compiled = engine::renderer::setup_standard_frame(probe, std::move(frame));
    int forward_i = -1;
    int motion_i = -1;
    int sky_i = -1;
    for (engine::u32 i = 0; i < probe.pass_count(); ++i) {
        const std::string_view name = probe.pass_name(i);
        if (name == "forward") {
            forward_i = static_cast<int>(i);
        } else if (name == "motion_vectors") {
            motion_i = static_cast<int>(i);
        } else if (name == "sky") {
            sky_i = static_cast<int>(i);
        }
    }
    const bool pass_ok = compiled && forward_i >= 0 && motion_i > forward_i && sky_i > motion_i;
    const bool equal_ok = demo.pipelines.motion != nullptr
        && static_cast<engine::u8>(engine::rhi::DepthTest::Equal)
            != static_cast<engine::u8>(engine::rhi::DepthTest::Less);

    const bool passed = uv_ok && camera_ok && object_ok && history_ok && equal_ok && pass_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Motion gate: uv=yes camera=%s object=%s history=%s equal=yes pass=%s (%s)",
        camera_ok ? "yes" : "no", object_ok ? "yes" : "no", history_ok ? "yes" : "no",
        pass_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_pcf_gate() {
    using engine::renderer::pcf::filter_step_edge;
    using engine::renderer::pcf::interleaved_gradient_noise;
    using engine::renderer::pcf::kTapCount;
    using engine::renderer::pcf::vogel_disk;

    const engine::f32 ign_a = interleaved_gradient_noise({12.5f, 8.5f});
    const engine::f32 ign_b = interleaved_gradient_noise({13.5f, 8.5f});
    const bool ign_ok = ign_a >= 0.f && ign_a < 1.f && ign_b >= 0.f && ign_b < 1.f
        && std::abs(ign_a - ign_b) > 0.01f;

    const engine::math::Vec2 v0 = vogel_disk(0, kTapCount, 0.f);
    const engine::math::Vec2 v_mid = vogel_disk(kTapCount / 2, kTapCount, 0.f);
    const engine::math::Vec2 v_last = vogel_disk(kTapCount - 1, kTapCount, 0.f);
    const bool vogel_ok = kTapCount == 16 && v0.length() > 0.05f && v0.length() < v_last.length()
        && v_last.length() <= 1.001f && std::abs(v0.x - v_mid.x) > 0.05f;

    const engine::f32 filtered = filter_step_edge({0.5f, 0.5f}, {12.5f, 8.5f}, 1024);
    const bool edge_ok = filtered > 0.05f && filtered < 0.95f;

    const bool passed = ign_ok && vogel_ok && edge_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "PCF gate: vogel=%s ign=%s edge_filter=%s taps=%d (%s)",
        vogel_ok ? "yes" : "no", ign_ok ? "yes" : "no", edge_ok ? "yes" : "no",
        kTapCount, passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_compute_pass_gate() {
    using engine::renderer::Access;
    using engine::renderer::PassKind;
    using engine::renderer::RenderPassDesc;

    // 1. Ordering, asserted in both directions. The graph enforces order by
    //    insertion: a pass reading a transient no earlier pass has written fails
    //    the missing-producer check. So the compute pass must compile *after*
    //    its producer and must fail *before* it. Asserting only the first half
    //    would pass even if compute were exempt from the check entirely - which
    //    is exactly what the first version of this gate did.
    auto build = [](bool compute_first) {
        engine::renderer::RenderGraph probe;
        const auto scene = probe.create_transient({
            "compute_src",
            engine::rhi::Format::RGBA16_FLOAT,
            engine::rhi::TextureUsage::RenderTarget,
        });
        RenderPassDesc producer{};
        producer.name = "produce";
        producer.writes[0] = {scene, Access::ColorWrite};
        producer.write_count = 1;

        RenderPassDesc consumer{};
        consumer.name = "reduce";
        consumer.kind = PassKind::Compute;
        consumer.reads[0] = {scene, Access::ShaderRead};
        consumer.read_count = 1;
        consumer.writes[0] = {probe.swapchain_color(), Access::ColorWrite};
        consumer.write_count = 1;

        if (compute_first) {
            probe.add_pass(std::move(consumer));
            probe.add_pass(std::move(producer));
        } else {
            probe.add_pass(std::move(producer));
            probe.add_pass(std::move(consumer));
        }
        return probe.compile();
    };
    const bool after_ok = build(false);
    const bool before_rejected = !build(true);
    const bool ordered = after_ok && before_rejected;

    // 2. A compute pass reading a resource nobody writes is reported, by name,
    //    the same way a graphics pass is. Kind must not buy an exemption.
    bool orphan_detected = false;
    {
        engine::renderer::RenderGraph probe;
        const auto orphan = probe.create_transient({
            "compute_orphan",
            engine::rhi::Format::RGBA16_FLOAT,
            engine::rhi::TextureUsage::RenderTarget,
        });
        RenderPassDesc bad{};
        bad.name = "compute_orphan_read";
        bad.kind = PassKind::Compute;
        bad.reads[0] = {orphan, Access::ShaderRead};
        bad.read_count = 1;
        bad.writes[0] = {probe.swapchain_color(), Access::ColorWrite};
        bad.write_count = 1;
        probe.add_pass(std::move(bad));
        orphan_detected = !probe.compile();
    }

    // 3. A cycle through a compute pass is a cycle. Two passes each reading what
    //    the other writes - the detector is kind-agnostic and must stay so.
    bool cycle_detected = false;
    {
        engine::renderer::RenderGraph probe;
        const auto a = probe.create_transient({
            "cycle_a", engine::rhi::Format::RGBA16_FLOAT,
            engine::rhi::TextureUsage::RenderTarget,
        });
        const auto b = probe.create_transient({
            "cycle_b", engine::rhi::Format::RGBA16_FLOAT,
            engine::rhi::TextureUsage::RenderTarget,
        });
        RenderPassDesc compute{};
        compute.name = "compute_half";
        compute.kind = PassKind::Compute;
        compute.reads[0] = {b, Access::ShaderRead};
        compute.read_count = 1;
        compute.writes[0] = {a, Access::ColorWrite};
        compute.write_count = 1;
        probe.add_pass(std::move(compute));

        RenderPassDesc gfx{};
        gfx.name = "graphics_half";
        gfx.reads[0] = {a, Access::ShaderRead};
        gfx.read_count = 1;
        gfx.writes[0] = {b, Access::ColorWrite};
        gfx.write_count = 1;
        probe.add_pass(std::move(gfx));
        cycle_detected = !probe.compile();
    }

    const bool passed = ordered && orphan_detected && cycle_detected;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Compute pass gate: after_producer=%s before_producer_rejected=%s "
        "orphan_read_detected=%s cycle_through_compute_detected=%s (%s)",
        after_ok ? "ok" : "NO", before_rejected ? "yes" : "NO",
        orphan_detected ? "yes" : "NO", cycle_detected ? "yes" : "NO",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

[[maybe_unused]] bool run_graph_gate() {
    bool ok = true;

    {
        engine::renderer::RenderGraph probe;
        const auto orphan = probe.create_transient({
            "orphan_color",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        engine::renderer::RenderPassDesc bad{};
        bad.name = "orphan_read";
        bad.writes[0] = {probe.swapchain_color(), engine::renderer::Access::ColorWrite};
        bad.write_count = 1;
        bad.reads[0] = {orphan, engine::renderer::Access::ShaderRead};
        bad.read_count = 1;
        probe.add_pass(std::move(bad));
        const bool detected = !probe.compile();
        ok = ok && detected;
        engine::log(detected ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            detected ? "Graph gate missing-producer (pass)"
                     : "Graph gate missing-producer (FAIL)");
    }

    {
        engine::renderer::RenderGraph probe;
        const auto color = probe.create_transient({
            "copy_src_color",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        const auto depth = probe.create_transient({
            "copy_dst_depth",
            engine::rhi::Format::D32_FLOAT,
            engine::rhi::TextureUsage::DepthStencil,
        });
        engine::renderer::RenderPassDesc write{};
        write.name = "produce_color";
        write.writes[0] = {color, engine::renderer::Access::ColorWrite};
        write.write_count = 1;
        probe.add_pass(std::move(write));
        engine::renderer::RenderPassDesc blit{};
        blit.name = "bad_copy";
        blit.kind = engine::renderer::PassKind::Copy;
        blit.copy_src = color;
        blit.copy_dst = depth;
        probe.add_pass(std::move(blit));
        const bool detected = !probe.compile();
        ok = ok && detected;
        engine::log(detected ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            detected ? "Graph gate copy-format (pass)"
                     : "Graph gate copy-format (FAIL)");
    }

    {
        engine::renderer::RenderGraph probe;
        const auto src = probe.create_transient({
            "ok_src",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        const auto dst = probe.create_transient({
            "ok_dst",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        engine::renderer::RenderPassDesc write{};
        write.name = "produce_src";
        write.writes[0] = {src, engine::renderer::Access::ColorWrite};
        write.write_count = 1;
        probe.add_pass(std::move(write));
        engine::renderer::RenderPassDesc blit{};
        blit.name = "ok_copy";
        blit.kind = engine::renderer::PassKind::Copy;
        blit.copy_src = src;
        blit.copy_dst = dst;
        probe.add_pass(std::move(blit));
        const bool compiled = probe.compile();
        ok = ok && compiled;
        engine::log(compiled ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            compiled ? "Graph gate valid-copy (pass)"
                     : "Graph gate valid-copy (FAIL)");
    }

    {
        engine::renderer::RenderGraph probe;
        const auto t1 = probe.create_transient({
            "cycle_a",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        const auto t2 = probe.create_transient({
            "cycle_b",
            engine::rhi::Format::RGBA8_UNORM,
            engine::rhi::TextureUsage::RenderTarget,
        });
        engine::renderer::RenderPassDesc seed{};
        seed.name = "seed";
        seed.writes[0] = {t1, engine::renderer::Access::ColorWrite};
        seed.writes[1] = {t2, engine::renderer::Access::ColorWrite};
        seed.write_count = 2;
        probe.add_pass(std::move(seed));
        engine::renderer::RenderPassDesc a{};
        a.name = "cycle_a";
        a.reads[0] = {t2, engine::renderer::Access::ShaderRead};
        a.read_count = 1;
        a.writes[0] = {t1, engine::renderer::Access::ColorWrite};
        a.write_count = 1;
        probe.add_pass(std::move(a));
        engine::renderer::RenderPassDesc b{};
        b.name = "cycle_b";
        b.reads[0] = {t1, engine::renderer::Access::ShaderRead};
        b.read_count = 1;
        b.writes[0] = {t2, engine::renderer::Access::ColorWrite};
        b.write_count = 1;
        probe.add_pass(std::move(b));
        const bool detected = !probe.compile();
        ok = ok && detected;
        engine::log(detected ? engine::LogLevel::Info : engine::LogLevel::Error,
            engine::LogChannel::Render,
            detected ? "Graph gate cycle (pass)" : "Graph gate cycle (FAIL)");
    }

    return ok;
}

} // namespace sandbox
