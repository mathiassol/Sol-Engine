#include "../sandbox_common.hpp"

// Asset, content and glTF gates.
//
// Moved out of main.cpp, which held all 72 and was 26% of the engine
// (analizeMax A4). What a gate *is* has not changed - see CLAUDE.md. Helpers
// private to these gates are `static` here; only what main.cpp also uses lives
// in sandbox_common.

namespace sandbox {

bool run_mount_gate(engine::assets::IAssetLoader& loader) {
    std::string physical;
    std::vector<engine::u8> bytes;
    const bool resolved = loader.resolve_path(kTestFile, physical);
    const bool loaded = resolved && loader.load_bytes(kTestFile, bytes) && !bytes.empty();
    char message[512];
    std::snprintf(message, sizeof(message),
        "Mount gate %s -> %s (%s)",
        kTestFile,
        physical.c_str(),
        loaded ? "pass" : "FAIL");
    engine::log(loaded ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return loaded;
}

// A mount is a containment boundary, so it has to actually contain. `..` used
// to be collapsed rather than rejected, and an absolute component silently
// replaced the root outright, because std::filesystem::operator/= discards its
// left side when the right has a root name.
bool run_mount_containment_gate(engine::assets::IAssetLoader& loader) {
    auto escapes = [&loader](const char* virtual_path) {
        std::string physical;
        return !loader.resolve_path(virtual_path, physical);
    };

    const bool dotdot = escapes("/content/../../../windows/win.ini");
    const bool unc = escapes("/content//server/share/secret");
    const bool rooted = escapes("/content//etc/passwd");
    // A drive letter is only an escape where drive letters exist. On Linux
    // "C:" is an ordinary directory name and `<root>/C:/Windows` stays inside
    // the mount, so asserting it unconditionally fails a correct engine - which
    // is what the first headless run on Linux did. Windows still gets the
    // assertion, because there it is a real vector.
#ifdef _WIN32
    const bool absolute = escapes("/content/C:/Windows/win.ini");
    const char* absolute_label = absolute ? "blocked" : "ESCAPED";
#else
    const bool absolute = true;
    const char* absolute_label = "n/a";
#endif
    // The legitimate path must still resolve, so this cannot pass by
    // rejecting everything.
    std::string ok_physical;
    const bool normal_ok = loader.resolve_path(kTestFile, ok_physical) && !ok_physical.empty();

    const bool passed = dotdot && absolute && unc && rooted && normal_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "Mount containment gate: dotdot=%s absolute=%s unc=%s rooted=%s normal_resolves=%s (%s)",
        dotdot ? "blocked" : "ESCAPED", absolute_label,
        unc ? "blocked" : "ESCAPED", rooted ? "blocked" : "ESCAPED",
        normal_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_handle_gate(engine::assets::gpu::GpuMeshStore& store, engine::rhi::IDevice& device,
    const engine::assets::MeshData& mesh_data) {
    const auto first = store.store(device, kCubeMesh, mesh_data);
    const auto second = store.store(device, kCubeMesh, mesh_data);
    const bool passed = first.valid() && first == second && store.get(second) != nullptr;
    char message[128];
    std::snprintf(message, sizeof(message),
        "MeshHandle gate: id=%llu gen=%u -> id=%llu gen=%u (%s)",
        static_cast<unsigned long long>(first.id), first.generation,
        static_cast<unsigned long long>(second.id), second.generation,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_handle_unload_gate(engine::assets::gpu::GpuMeshStore& store, engine::rhi::IDevice& device,
    const engine::assets::MeshData& mesh_data) {
    constexpr const char* kProbe = "/content/meshes/cube.obj#unload-probe";
    const auto first = store.store(device, kProbe, mesh_data);
    device.wait_idle();
    const bool dropped = first.valid() && store.unload(first) && store.get(first) == nullptr;
    const auto second = store.store(device, kProbe, mesh_data);
    const bool passed = dropped && first.valid() && second.valid() && first != second
        && store.get(first) == nullptr && store.get(second) != nullptr;
    store.unload(second);
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets,
        passed ? "MeshHandle unload gate (pass)" : "MeshHandle unload gate (FAIL)");
    return passed;
}

bool run_mesh_reload_gate(engine::rhi::IDevice& device, const engine::assets::MeshData& mesh_data) {
    device.wait_idle();
    const auto before = device.gpu_memory_stats();

    constexpr engine::usize kDummyBytes = 1024u * 1024u;
    for (int i = 0; i < 100; ++i) {
        auto transient = engine::assets::gpu::upload_mesh(device, mesh_data);
        engine::rhi::BufferDesc dummy{};
        dummy.size  = kDummyBytes;
        dummy.usage = engine::rhi::BufferUsage::Vertex;
        auto dummy_buffer = device.create_buffer(dummy);
        (void)transient;
        (void)dummy_buffer;
    }

    device.wait_idle();
    const auto after = device.gpu_memory_stats();

    constexpr engine::u64 kSlackBytes = 8ull * 1024ull * 1024ull;
    const bool passed = after.local_usage_bytes <= before.local_usage_bytes + kSlackBytes;

    char message[256];
    std::snprintf(message, sizeof(message),
        "Mesh reload gate (100x + 1 MiB dummy): local VRAM %llu -> %llu bytes (%s)",
        static_cast<unsigned long long>(before.local_usage_bytes),
        static_cast<unsigned long long>(after.local_usage_bytes),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Render, message);
    return passed;
}

bool run_cook_gate() {
    using engine::assets::CookedAudio;
    using engine::assets::CookedKind;
    using engine::assets::ImageData;
    using engine::assets::MeshData;

    MeshData mesh{};
    mesh.vertices = {
        {0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f},
    };
    mesh.indices = {0, 1, 2};
    engine::assets::compute_mesh_bounds(mesh);

    std::vector<engine::u8> mesh_blob;
    MeshData mesh_loaded{};
    CookedKind kind{};
    ImageData wrong_image{};
    const bool mesh_ok = engine::assets::write_cooked_mesh(mesh, mesh_blob)
        && engine::assets::peek_cooked_kind(mesh_blob, kind)
        && kind == CookedKind::Mesh
        && engine::assets::read_cooked_mesh(mesh_blob, mesh_loaded)
        && mesh_loaded.vertices.size() == 3
        && mesh_loaded.indices.size() == 3
        && mesh_loaded.indices[2] == 2
        && std::abs(mesh_loaded.vertices[1].px - 1.f) < 1.e-5f
        && std::abs(mesh_loaded.bounds.max.x - 1.f) < 1.e-4f
        && std::abs(mesh_loaded.bounds.max.z - 1.f) < 1.e-4f
        && !engine::assets::read_cooked_image(mesh_blob, wrong_image);

    ImageData image{};
    image.width = 2;
    image.height = 2;
    image.rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };
    std::vector<engine::u8> image_blob;
    ImageData image_loaded{};
    MeshData wrong_mesh{};
    const bool image_ok = engine::assets::write_cooked_image(image, image_blob)
        && engine::assets::peek_cooked_kind(image_blob, kind)
        && kind == CookedKind::Image
        && engine::assets::read_cooked_image(image_blob, image_loaded)
        && image_loaded.width == 2
        && image_loaded.height == 2
        && image_loaded.rgba.size() == 16
        && image_loaded.rgba[0] == 255
        && image_loaded.rgba[5] == 255
        && !engine::assets::read_cooked_mesh(image_blob, wrong_mesh);

    CookedAudio audio{};
    audio.sample_rate = 44100;
    audio.channels = 1;
    audio.bits_per_sample = 16;
    audio.pcm = {0, 0, 0, 1, 0, 2, 0, 3};
    std::vector<engine::u8> audio_blob;
    CookedAudio audio_loaded{};
    const bool audio_ok = engine::assets::write_cooked_audio(audio, audio_blob)
        && engine::assets::peek_cooked_kind(audio_blob, kind)
        && kind == CookedKind::Audio
        && engine::assets::read_cooked_audio(audio_blob, audio_loaded)
        && audio_loaded.sample_rate == 44100
        && audio_loaded.channels == 1
        && audio_loaded.bits_per_sample == 16
        && audio_loaded.pcm == audio.pcm
        && !engine::assets::read_cooked_mesh(audio_blob, wrong_mesh);

    MeshData empty_mesh{};
    std::vector<engine::u8> scratch;
    std::vector<engine::u8> truncated = mesh_blob;
    if (!truncated.empty()) {
        truncated.pop_back();
    }
    std::vector<engine::u8> bad_magic = mesh_blob;
    if (!bad_magic.empty()) {
        bad_magic[0] = 'X';
    }
    std::vector<engine::u8> bad_version = mesh_blob;
    if (bad_version.size() >= 8) {
        bad_version[4] = 99;
        bad_version[5] = 0;
        bad_version[6] = 0;
        bad_version[7] = 0;
    }
    ImageData empty_image{};
    CookedAudio bad_audio = audio;
    bad_audio.channels = 3;

    const bool reject_ok = !engine::assets::write_cooked_mesh(empty_mesh, scratch)
        && !engine::assets::write_cooked_image(empty_image, scratch)
        && !engine::assets::write_cooked_audio(bad_audio, scratch)
        && !engine::assets::peek_cooked_kind({}, kind)
        && !engine::assets::read_cooked_mesh(truncated, mesh_loaded)
        && !engine::assets::read_cooked_mesh(bad_magic, mesh_loaded)
        && !engine::assets::read_cooked_mesh(bad_version, mesh_loaded);

    const bool passed = mesh_ok && image_ok && audio_ok && reject_ok;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Cook gate: mesh=yes image=yes audio=yes reject=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_pak_gate() {
    using engine::assets::ImageData;
    using engine::assets::MeshData;
    using engine::assets::PakEntry;

    MeshData mesh{};
    mesh.vertices = {
        {0.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f, 0.f, 1.f, 0.f, 0.f, 1.f},
    };
    mesh.indices = {0, 1, 2};
    engine::assets::compute_mesh_bounds(mesh);

    ImageData image{};
    image.width = 2;
    image.height = 2;
    image.rgba = {
        255, 0, 0, 255,
        0, 255, 0, 255,
        0, 0, 255, 255,
        255, 255, 255, 255,
    };

    std::vector<engine::u8> mesh_blob;
    std::vector<engine::u8> image_blob;
    const bool cooked = engine::assets::write_cooked_mesh(mesh, mesh_blob)
        && engine::assets::write_cooked_image(image, image_blob);

    std::vector<PakEntry> entries(2);
    entries[0].name = "/content/tri.solc";
    entries[0].bytes = mesh_blob;
    entries[1].name = "/content/px.solc";
    entries[1].bytes = image_blob;

    std::vector<engine::u8> pak;
    std::vector<engine::u8> got_mesh;
    std::vector<engine::u8> got_image;
    MeshData mesh_loaded{};
    ImageData image_loaded{};
    const bool pack_ok = cooked && engine::assets::write_pak(entries, pak)
        && engine::assets::peek_pak(pak)
        && engine::assets::read_pak_entry(pak, "/content/tri.solc", got_mesh)
        && engine::assets::read_pak_entry(pak, "/content/px.solc", got_image)
        && got_mesh == mesh_blob
        && got_image == image_blob
        && engine::assets::read_cooked_mesh(got_mesh, mesh_loaded)
        && engine::assets::read_cooked_image(got_image, image_loaded)
        && mesh_loaded.indices.size() == 3
        && image_loaded.width == 2;

    std::vector<engine::u8> miss_bytes;
    const bool miss_ok = pack_ok
        && !engine::assets::read_pak_entry(pak, "/content/nope.solc", miss_bytes)
        && !engine::assets::read_pak_entry(pak, "content/tri.solc", miss_bytes);

    auto loader = engine::assets::create_pak_loader(pak);
    std::vector<engine::u8> loaded;
    std::string resolved;
    const bool get_ok = miss_ok && loader
        && loader->load_bytes("/content/tri.solc", loaded)
        && loaded == mesh_blob
        && loader->resolve_path("/content/px.solc", resolved)
        && resolved == "/content/px.solc"
        && !loader->load_bytes("/content/nope.solc", loaded);

    std::vector<engine::u8> scratch;
    std::vector<PakEntry> empty;
    std::vector<PakEntry> dup = entries;
    dup.push_back(entries[0]);
    std::vector<PakEntry> traversal(1);
    traversal[0].name = "/content/../secret.solc";
    traversal[0].bytes = {1, 2, 3, 4};
    std::vector<engine::u8> truncated = pak;
    if (!truncated.empty()) {
        truncated.pop_back();
    }
    std::vector<engine::u8> bad_magic = pak;
    if (!bad_magic.empty()) {
        bad_magic[0] = 'X';
    }

    const bool reject_ok = !engine::assets::write_pak(empty, scratch)
        && !engine::assets::write_pak(dup, scratch)
        && !engine::assets::write_pak(traversal, scratch)
        && !engine::assets::peek_pak({})
        && !engine::assets::peek_pak(truncated)
        && !engine::assets::peek_pak(bad_magic)
        && engine::assets::create_pak_loader(bad_magic) == nullptr;

    const bool passed = pack_ok && get_ok && miss_ok && reject_ok;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Pak gate: pack=yes get=yes miss=yes reject=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_pack_gate(engine::Engine& app) {
    engine::platform::IFileSystem* fs = app.filesystem();
    const std::filesystem::path pak_path =
        std::filesystem::path(app.executable_directory()) / "content.pak";

    std::vector<engine::u8> bytes;
    const bool file_ok = fs != nullptr && fs->exists(pak_path.string())
        && fs->read_file(pak_path.string(), bytes) && !bytes.empty();
    const bool peek_ok = file_ok && engine::assets::peek_pak(bytes);

    std::vector<engine::u8> cube_blob;
    engine::assets::MeshData cube{};
    const bool get_ok = peek_ok
        && engine::assets::read_pak_entry(bytes, "/content/meshes/cube.solc", cube_blob)
        && engine::assets::read_cooked_mesh(cube_blob, cube)
        && cube.vertices.size() >= 3
        && cube.indices.size() >= 3;

    std::vector<engine::u8> husky_blob;
    engine::assets::MeshData husky{};
    const bool husky_ok = get_ok
        && engine::assets::read_pak_entry(bytes, "/content/meshes/cartoon_husky.solc", husky_blob)
        && engine::assets::read_cooked_mesh(husky_blob, husky)
        && husky.vertices.size() > 100;

    const bool passed = file_ok && peek_ok && get_ok && husky_ok;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Pack gate: file=yes peek=yes get=yes (%s)",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_albedo_gate(const engine::assets::ImageData& image,
    const engine::rhi::ITexture& texture) {
    const bool size_ok = image.width > 0 && image.height > 0
        && image.rgba.size() == static_cast<engine::usize>(image.width) * image.height * 4;
    // Albedo is colour, so it has to be created sRGB or the forward pass runs its
    // lighting maths on encoded values. Nothing else covers this: the colour
    // space gate proves the curve and the mip averaging, but it runs before any
    // albedo exists, so a revert to RGBA8_UNORM here would leave every gate green.
    const bool srgb_ok = texture.format() == engine::rhi::Format::RGBA8_UNORM_SRGB;
    const bool passed = size_ok && srgb_ok;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Albedo PNG gate: %ux%u rgba=%zu srgb=%s (%s)",
        image.width, image.height, image.rgba.size(),
        srgb_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_gltf_gate(const engine::assets::gltf::GltfLoadResult& loaded) {
    const bool mesh_ok = loaded.mesh.vertices.size() >= 1000 && loaded.mesh.indices.size() >= 3000
        && loaded.mesh.bounds.valid();
    const bool albedo_ok = !loaded.albedo_uri.empty();
    const bool pbr_ok = loaded.metallic >= 0.f && loaded.metallic <= 1.f
        && loaded.roughness >= 0.f && loaded.roughness <= 1.f;
    const bool passed = mesh_ok && albedo_ok && pbr_ok;
    char message[224];
    std::snprintf(message, sizeof(message),
        "glTF gate: verts=%zu indices=%zu albedo=%s metal=%.2f rough=%.2f (%s)",
        loaded.mesh.vertices.size(), loaded.mesh.indices.size(),
        albedo_ok ? "yes" : "no", loaded.metallic, loaded.roughness,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

static void append_le_u32(std::vector<engine::u8>& out, engine::u32 value) {
    out.push_back(static_cast<engine::u8>(value));
    out.push_back(static_cast<engine::u8>(value >> 8));
    out.push_back(static_cast<engine::u8>(value >> 16));
    out.push_back(static_cast<engine::u8>(value >> 24));
}

static void append_le_f32(std::vector<engine::u8>& out, engine::f32 value) {
    engine::u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_le_u32(out, bits);
}

static void append_vec3(std::vector<engine::u8>& out, engine::f32 x, engine::f32 y, engine::f32 z) {
    append_le_f32(out, x);
    append_le_f32(out, y);
    append_le_f32(out, z);
}

static void append_vec2(std::vector<engine::u8>& out, engine::f32 x, engine::f32 y) {
    append_le_f32(out, x);
    append_le_f32(out, y);
}

static bool write_gltf_extras_probe(const std::filesystem::path& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }

    std::vector<engine::u8> bin;
    // primitive 0: triangle in XY at origin
    append_vec3(bin, 0.f, 0.f, 0.f);
    append_vec3(bin, 1.f, 0.f, 0.f);
    append_vec3(bin, 0.f, 1.f, 0.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec2(bin, 0.f, 0.f);
    append_vec2(bin, 1.f, 0.f);
    append_vec2(bin, 0.f, 1.f);
    append_le_u32(bin, 0);
    append_le_u32(bin, 1);
    append_le_u32(bin, 2);
    // primitive 1: triangle shifted +X
    append_vec3(bin, 2.f, 0.f, 0.f);
    append_vec3(bin, 3.f, 0.f, 0.f);
    append_vec3(bin, 2.f, 1.f, 0.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec2(bin, 0.f, 0.f);
    append_vec2(bin, 1.f, 0.f);
    append_vec2(bin, 0.f, 1.f);
    append_le_u32(bin, 0);
    append_le_u32(bin, 1);
    append_le_u32(bin, 2);

    const auto bin_path = dir / "probe.bin";
    std::ofstream bin_file(bin_path, std::ios::binary | std::ios::trunc);
    if (!bin_file) {
        return false;
    }
    bin_file.write(reinterpret_cast<const char*>(bin.data()),
        static_cast<std::streamsize>(bin.size()));
    if (!bin_file) {
        return false;
    }
    bin_file.close();

    const char* json =
        "{\n"
        "  \"asset\": { \"version\": \"2.0\" },\n"
        "  \"meshes\": [{ \"primitives\": [\n"
        "    { \"attributes\": { \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 },\n"
        "      \"indices\": 3, \"material\": 0 },\n"
        "    { \"attributes\": { \"POSITION\": 4, \"NORMAL\": 5, \"TEXCOORD_0\": 6 },\n"
        "      \"indices\": 7, \"material\": 1 }\n"
        "  ] }],\n"
        "  \"materials\": [\n"
        "    { \"pbrMetallicRoughness\": {\n"
        "        \"baseColorTexture\": { \"index\": 0 },\n"
        "        \"metallicRoughnessTexture\": { \"index\": 1 },\n"
        "        \"metallicFactor\": 0.25, \"roughnessFactor\": 0.5 },\n"
        "      \"normalTexture\": { \"index\": 2 } },\n"
        "    { \"pbrMetallicRoughness\": {\n"
        "        \"baseColorTexture\": { \"index\": 3 },\n"
        "        \"metallicFactor\": 0.0, \"roughnessFactor\": 1.0 } }\n"
        "  ],\n"
        "  \"textures\": [ { \"source\": 0 }, { \"source\": 1 }, "
        "{ \"source\": 2 }, { \"source\": 3 } ],\n"
        "  \"images\": [\n"
        "    { \"uri\": \"a.png\" }, { \"uri\": \"mr.png\" }, "
        "{ \"uri\": \"n.png\" }, { \"uri\": \"b.png\" }\n"
        "  ],\n"
        "  \"buffers\": [ { \"uri\": \"probe.bin\", \"byteLength\": 216 } ],\n"
        "  \"bufferViews\": [\n"
        "    { \"buffer\": 0, \"byteOffset\": 0, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 12 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 108, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 144, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 180, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 204, \"byteLength\": 12 }\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" },\n"
        "    { \"bufferView\": 4, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 5, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 6, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 7, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" }\n"
        "  ]\n"
        "}\n";

    std::ofstream gltf_file(dir / "probe.gltf", std::ios::binary | std::ios::trunc);
    if (!gltf_file) {
        return false;
    }
    gltf_file << json;
    return static_cast<bool>(gltf_file);
}

static bool uri_ends_with(const std::string& uri, const char* suffix) {
    const std::string_view view{uri};
    const std::string_view end{suffix};
    return view.size() >= end.size()
        && view.substr(view.size() - end.size()) == end;
}

bool run_gltf_extras_gate() {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "sol-engine-gltf-extras";
    const bool wrote = write_gltf_extras_probe(dir);
    engine::assets::gltf::GltfLoadResult loaded{};
    bool loaded_ok = false;
    if (wrote) {
        auto loader = engine::assets::gltf::create_mesh_loader();
        loaded_ok = loader && loader->load((dir / "probe.gltf").string(), loaded);
    }

    const bool prims_ok = loaded_ok && loaded.primitives.size() == 2
        && loaded.mesh.vertices.size() == 6 && loaded.mesh.indices.size() == 6
        && loaded.primitives[0].first_index == 0 && loaded.primitives[0].index_count == 3
        && loaded.primitives[1].first_index == 3 && loaded.primitives[1].index_count == 3
        && loaded.mesh.indices[3] == 3;
    const bool mr_ok = prims_ok
        && uri_ends_with(loaded.primitives[0].metallic_roughness_uri, "mr.png")
        && std::abs(loaded.primitives[0].metallic - 0.25f) < 1.e-4f
        && std::abs(loaded.primitives[0].roughness - 0.5f) < 1.e-4f;
    const bool normal_ok = prims_ok && uri_ends_with(loaded.primitives[0].normal_uri, "n.png")
        && uri_ends_with(loaded.primitives[0].albedo_uri, "a.png")
        && uri_ends_with(loaded.primitives[1].albedo_uri, "b.png");
    const bool passed = prims_ok && mr_ok && normal_ok;

    char message[192];
    std::snprintf(message, sizeof(message),
        "glTF extras gate: prims=%s mr=%s normal=%s (%s)",
        prims_ok ? "yes" : "no", mr_ok ? "yes" : "no", normal_ok ? "yes" : "no",
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

// One valid triangle: 3 positions, 3 normals, 3 UVs, 3 u32 indices = 108 bytes.
// Callers pass a JSON body describing the same buffer, malformed in one way.
static bool write_gltf_probe_with_json(const std::filesystem::path& dir, const char* json) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return false;
    }

    std::vector<engine::u8> bin;
    append_vec3(bin, 0.f, 0.f, 0.f);
    append_vec3(bin, 1.f, 0.f, 0.f);
    append_vec3(bin, 0.f, 1.f, 0.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec3(bin, 0.f, 0.f, 1.f);
    append_vec2(bin, 0.f, 0.f);
    append_vec2(bin, 1.f, 0.f);
    append_vec2(bin, 0.f, 1.f);
    append_le_u32(bin, 0);
    append_le_u32(bin, 1);
    append_le_u32(bin, 2);

    std::ofstream bin_file(dir / "probe.bin", std::ios::binary | std::ios::trunc);
    if (!bin_file) {
        return false;
    }
    bin_file.write(reinterpret_cast<const char*>(bin.data()),
        static_cast<std::streamsize>(bin.size()));
    if (!bin_file) {
        return false;
    }
    bin_file.close();

    std::ofstream gltf_file(dir / "probe.gltf", std::ios::binary | std::ios::trunc);
    if (!gltf_file) {
        return false;
    }
    gltf_file << json;
    return static_cast<bool>(gltf_file);
}

// Returns true when the loader REFUSES the file. A malformed glTF must be
// rejected, not parsed into an out-of-bounds read.
static bool gltf_probe_rejected(const char* name, const char* json) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "sol-engine-gltf-validate" / name;
    if (!write_gltf_probe_with_json(dir, json)) {
        return false;
    }
    auto loader = engine::assets::gltf::create_mesh_loader();
    if (!loader) {
        return false;
    }
    engine::assets::gltf::GltfLoadResult loaded{};
    return !loader->load((dir / "probe.gltf").string(), loaded);
}

bool run_gltf_validate_gate() {
    // Baseline: the well-formed version of the same buffer must still load, so
    // this gate cannot pass by rejecting everything.
    const char* good =
        "{\n"
        "  \"asset\": { \"version\": \"2.0\" },\n"
        "  \"meshes\": [{ \"primitives\": [ { \"attributes\": "
        "{ \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 }, \"indices\": 3 } ] }],\n"
        "  \"buffers\": [ { \"uri\": \"probe.bin\", \"byteLength\": 108 } ],\n"
        "  \"bufferViews\": [\n"
        "    { \"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 12 }\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" }\n"
        "  ]\n"
        "}\n";

    // POSITION claims 1000 VEC3 (12000 bytes) inside a 36-byte view. Unpacking
    // this without validation memcpy's ~12 KB out of a 108-byte allocation.
    const char* overrun_accessor =
        "{\n"
        "  \"asset\": { \"version\": \"2.0\" },\n"
        "  \"meshes\": [{ \"primitives\": [ { \"attributes\": "
        "{ \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 }, \"indices\": 3 } ] }],\n"
        "  \"buffers\": [ { \"uri\": \"probe.bin\", \"byteLength\": 108 } ],\n"
        "  \"bufferViews\": [\n"
        "    { \"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 12 }\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 1000, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" }\n"
        "  ]\n"
        "}\n";

    // bufferView 0 starts ~1 GB past the end of a 108-byte buffer. Reading it
    // without validation dereferences a wild displaced pointer.
    const char* overrun_view =
        "{\n"
        "  \"asset\": { \"version\": \"2.0\" },\n"
        "  \"meshes\": [{ \"primitives\": [ { \"attributes\": "
        "{ \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 }, \"indices\": 3 } ] }],\n"
        "  \"buffers\": [ { \"uri\": \"probe.bin\", \"byteLength\": 108 } ],\n"
        "  \"bufferViews\": [\n"
        "    { \"buffer\": 0, \"byteOffset\": 1000000000, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 12 }\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" }\n"
        "  ]\n"
        "}\n";

    // Indices read from the NORMAL view as u32, so they carry the bit patterns
    // of 1.0f — index ~1.07e9 against 3 vertices. Unvalidated, this reaches a
    // D3D12 index buffer and the draw reads outside the bound resource: device
    // removal or silent garbage geometry.
    const char* overrun_indices =
        "{\n"
        "  \"asset\": { \"version\": \"2.0\" },\n"
        "  \"meshes\": [{ \"primitives\": [ { \"attributes\": "
        "{ \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 }, \"indices\": 3 } ] }],\n"
        "  \"buffers\": [ { \"uri\": \"probe.bin\", \"byteLength\": 108 } ],\n"
        "  \"bufferViews\": [\n"
        "    { \"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 12 }\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" }\n"
        "  ]\n"
        "}\n";

    const std::filesystem::path good_dir =
        std::filesystem::temp_directory_path() / "sol-engine-gltf-validate" / "good";
    bool good_ok = false;
    if (write_gltf_probe_with_json(good_dir, good)) {
        auto loader = engine::assets::gltf::create_mesh_loader();
        engine::assets::gltf::GltfLoadResult loaded{};
        good_ok = loader && loader->load((good_dir / "probe.gltf").string(), loaded)
            && loaded.mesh.vertices.size() == 3 && loaded.mesh.indices.size() == 3;
    }

    const bool accessor_ok = gltf_probe_rejected("overrun_accessor", overrun_accessor);
    const bool view_ok = gltf_probe_rejected("overrun_view", overrun_view);
    const bool indices_ok = gltf_probe_rejected("overrun_indices", overrun_indices);
    const bool passed = good_ok && accessor_ok && view_ok && indices_ok;

    char message[224];
    std::snprintf(message, sizeof(message),
        "glTF validate gate: valid_loads=%s accessor_overrun_rejected=%s "
        "view_overrun_rejected=%s index_overrun_rejected=%s (%s)",
        good_ok ? "yes" : "no", accessor_ok ? "yes" : "no", view_ok ? "yes" : "no",
        indices_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

// Same 108-byte buffer as the validate probes: one triangle at (0,0,0),
// (1,0,0), (0,1,0) with every normal (0,0,1). Only the node graph varies.
static std::string gltf_with_nodes(const char* nodes_json, const char* roots) {
    return std::string(
        "{\n"
        "  \"asset\": { \"version\": \"2.0\" },\n"
        "  \"scene\": 0,\n"
        "  \"scenes\": [ { \"nodes\": [")
        + roots +
        "] } ],\n"
        "  \"nodes\": " + nodes_json + ",\n"
        "  \"meshes\": [{ \"primitives\": [ { \"attributes\": "
        "{ \"POSITION\": 0, \"NORMAL\": 1, \"TEXCOORD_0\": 2 }, \"indices\": 3 } ] }],\n"
        "  \"buffers\": [ { \"uri\": \"probe.bin\", \"byteLength\": 108 } ],\n"
        "  \"bufferViews\": [\n"
        "    { \"buffer\": 0, \"byteOffset\": 0,  \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 36, \"byteLength\": 36 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 72, \"byteLength\": 24 },\n"
        "    { \"buffer\": 0, \"byteOffset\": 96, \"byteLength\": 12 }\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    { \"bufferView\": 0, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 1, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC3\" },\n"
        "    { \"bufferView\": 2, \"componentType\": 5126, \"count\": 3, \"type\": \"VEC2\" },\n"
        "    { \"bufferView\": 3, \"componentType\": 5125, \"count\": 3, \"type\": \"SCALAR\" }\n"
        "  ]\n"
        "}\n";
}

static bool load_node_probe(const char* name, const std::string& json,
    engine::assets::gltf::GltfLoadResult& out) {
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "sol-engine-gltf-nodes" / name;
    if (!write_gltf_probe_with_json(dir, json.c_str())) {
        return false;
    }
    auto loader = engine::assets::gltf::create_mesh_loader();
    return loader && loader->load((dir / "probe.gltf").string(), out);
}

static bool near_eq(engine::f32 a, engine::f32 b) {
    return std::abs(a - b) < 1.e-4f;
}

bool run_gltf_node_transform_gate() {
    using engine::assets::gltf::GltfLoadResult;

    // Translation on the node that owns the mesh. Before node transforms were
    // read, every one of these cases collapsed to the origin.
    GltfLoadResult translated{};
    const bool translate_ok =
        load_node_probe("translate",
            gltf_with_nodes("[ { \"mesh\": 0, \"translation\": [10, 0, 0] } ]", "0"), translated)
        && translated.mesh.vertices.size() == 3
        && near_eq(translated.mesh.vertices[0].px, 10.f)
        && near_eq(translated.mesh.vertices[1].px, 11.f)
        && near_eq(translated.mesh.bounds.min.x, 10.f)
        && near_eq(translated.mesh.bounds.max.x, 11.f);

    // One mesh referenced by two nodes is two placements, not one. Node 0 is
    // untransformed so this also exercises the identity fast path.
    GltfLoadResult instanced{};
    const bool instance_ok =
        load_node_probe("instanced",
            gltf_with_nodes("[ { \"mesh\": 0 }, { \"mesh\": 0, \"translation\": [5, 0, 0] } ]",
                "0, 1"), instanced)
        && instanced.mesh.vertices.size() == 6 && instanced.mesh.indices.size() == 6
        && instanced.primitives.size() == 2
        && near_eq(instanced.mesh.vertices[0].px, 0.f)
        && near_eq(instanced.mesh.vertices[3].px, 5.f)
        && instanced.mesh.indices[3] == 3;

    // A child composes with its parent - the whole chain, not just one level.
    GltfLoadResult nested{};
    const bool nested_ok =
        load_node_probe("nested",
            gltf_with_nodes(
                "[ { \"children\": [1], \"translation\": [1, 0, 0] },\n"
                "   { \"mesh\": 0, \"translation\": [0, 2, 0] } ]", "0"), nested)
        && nested.mesh.vertices.size() == 3
        && near_eq(nested.mesh.vertices[0].px, 1.f)
        && near_eq(nested.mesh.vertices[0].py, 2.f);

    // Negative scale mirrors the geometry, which reverses triangle orientation.
    // The engine rasterizes FrontCounterClockwise, so the winding must flip or
    // the part renders inside-out.
    GltfLoadResult mirrored{};
    const bool mirror_ok =
        load_node_probe("mirrored",
            gltf_with_nodes("[ { \"mesh\": 0, \"scale\": [-1, 1, 1] } ]", "0"), mirrored)
        && mirrored.mesh.vertices.size() == 3 && mirrored.mesh.indices.size() == 3
        && near_eq(mirrored.mesh.vertices[1].px, -1.f)
        && mirrored.mesh.indices[0] == 0 && mirrored.mesh.indices[1] == 2
        && mirrored.mesh.indices[2] == 1;

    // Column-major 90 degrees about X: position (0,1,0) -> (0,0,1), and the
    // normal (0,0,1) -> (0,-1,0). Proves normals are transformed too, not just
    // positions - a rotated part would otherwise be lit as if unrotated.
    GltfLoadResult rotated{};
    const bool rotate_ok =
        load_node_probe("rotated",
            gltf_with_nodes("[ { \"mesh\": 0, \"matrix\": "
                "[1,0,0,0, 0,0,1,0, 0,-1,0,0, 0,0,0,1] } ]", "0"), rotated)
        && rotated.mesh.vertices.size() == 3
        && near_eq(rotated.mesh.vertices[2].pz, 1.f)
        && near_eq(rotated.mesh.vertices[2].py, 0.f)
        && near_eq(rotated.mesh.vertices[0].ny, -1.f)
        && near_eq(rotated.mesh.vertices[0].nz, 0.f);

    const bool passed = translate_ok && instance_ok && nested_ok && mirror_ok && rotate_ok;

    char message[224];
    std::snprintf(message, sizeof(message),
        "glTF node transform gate: translate=%s two_nodes=%s nested=%s "
        "mirror_winding=%s rotate_normals=%s (%s)",
        translate_ok ? "yes" : "no", instance_ok ? "yes" : "no", nested_ok ? "yes" : "no",
        mirror_ok ? "yes" : "no", rotate_ok ? "yes" : "no", passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_husky_mesh_gate(const engine::assets::MeshData& mesh) {
    const bool passed = mesh.vertices.size() >= 1000 && mesh.indices.size() >= 3000;
    char message[160];
    std::snprintf(message, sizeof(message),
        "Husky mesh gate: verts=%zu indices=%zu (%s)",
        mesh.vertices.size(), mesh.indices.size(),
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_aabb_gate(const engine::assets::MeshData& mesh) {
    const auto& box = mesh.bounds;
    const engine::f32 dx = box.max.x - box.min.x;
    const engine::f32 dy = box.max.y - box.min.y;
    const engine::f32 dz = box.max.z - box.min.z;
    const bool passed = box.valid() && dx > 0.01f && dy > 0.01f && dz > 0.01f;
    char message[192];
    std::snprintf(message, sizeof(message),
        "Husky AABB gate: min=(%.3f,%.3f,%.3f) max=(%.3f,%.3f,%.3f) (%s)",
        box.min.x, box.min.y, box.min.z, box.max.x, box.max.y, box.max.z,
        passed ? "pass" : "FAIL");
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets, message);
    return passed;
}

bool run_aabb_transform_gate(const engine::assets::MeshData& mesh) {
    const engine::math::Aabb local = mesh.bounds;
    const engine::math::Aabb moved
        = local.transformed(engine::math::Mat4::translate({1.f, 0.f, 0.f}));
    const bool passed = local.valid() && moved.valid()
        && moved.min.x > local.min.x + 0.5f
        && moved.max.x > local.max.x + 0.5f;
    engine::log(passed ? engine::LogLevel::Info : engine::LogLevel::Error,
        engine::LogChannel::Assets,
        passed ? "AABB transform gate: +X shift (pass)"
               : "AABB transform gate: +X shift (FAIL)");
    return passed;
}

} // namespace sandbox
