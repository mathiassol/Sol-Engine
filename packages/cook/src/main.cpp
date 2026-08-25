#include <engine/assets/cooked.hpp>
#include <engine/assets/gltf/mesh_loader_gltf.hpp>
#include <engine/assets/obj/mesh_loader_obj.hpp>
#include <engine/assets/pak.hpp>
#include <engine/assets/png/image_loader_png.hpp>
#include <engine/core/log.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

bool write_bytes(const std::filesystem::path& path, std::span<const engine::u8> data) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()),
        static_cast<std::streamsize>(data.size()));
    return file.good();
}

bool cook_mesh_obj(const std::filesystem::path& src, const char* virtual_name,
    std::vector<engine::assets::PakEntry>& entries) {
    auto loader = engine::assets::obj::create_mesh_loader();
    engine::assets::MeshData mesh{};
    if (!loader || !loader->load(src.string(), mesh)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Cook failed to load OBJ ") + src.string());
        return false;
    }
    engine::assets::compute_mesh_bounds(mesh);
    engine::assets::PakEntry entry{};
    entry.name = virtual_name;
    if (!engine::assets::write_cooked_mesh(mesh, entry.bytes)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Cook failed to pack mesh ") + virtual_name);
        return false;
    }
    entries.push_back(std::move(entry));
    return true;
}

bool cook_mesh_gltf(const std::filesystem::path& src, const char* virtual_name,
    std::vector<engine::assets::PakEntry>& entries) {
    auto loader = engine::assets::gltf::create_mesh_loader();
    engine::assets::gltf::GltfLoadResult result{};
    if (!loader || !loader->load(src.string(), result)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Cook failed to load glTF ") + src.string());
        return false;
    }
    engine::assets::compute_mesh_bounds(result.mesh);
    engine::assets::PakEntry entry{};
    entry.name = virtual_name;
    if (!engine::assets::write_cooked_mesh(result.mesh, entry.bytes)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Cook failed to pack mesh ") + virtual_name);
        return false;
    }
    entries.push_back(std::move(entry));
    return true;
}

bool cook_image(const std::filesystem::path& src, const char* virtual_name,
    std::vector<engine::assets::PakEntry>& entries) {
    engine::assets::ImageData image{};
    if (!engine::assets::png::load_png_file(src.string(), image)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Cook failed to load PNG ") + src.string());
        return false;
    }
    engine::assets::PakEntry entry{};
    entry.name = virtual_name;
    if (!engine::assets::write_cooked_image(image, entry.bytes)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Cook failed to pack image ") + virtual_name);
        return false;
    }
    entries.push_back(std::move(entry));
    return true;
}

bool cook_beep(std::vector<engine::assets::PakEntry>& entries) {
    engine::assets::CookedAudio audio{};
    audio.sample_rate = 44100;
    audio.channels = 1;
    audio.bits_per_sample = 16;
    audio.pcm = {0, 0, 0, 16, 0, 32, 0, 16, 0, 0, 0, 240, 0, 224, 0, 240};
    engine::assets::PakEntry entry{};
    entry.name = "/content/audio/beep.solc";
    if (!engine::assets::write_cooked_audio(audio, entry.bytes)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets, "Cook failed to pack beep");
        return false;
    }
    entries.push_back(std::move(entry));
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path content;
    std::filesystem::path out;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--content" && i + 1 < argc) {
            content = argv[++i];
        } else if (arg == "--out" && i + 1 < argc) {
            out = argv[++i];
        }
    }
    if (content.empty() || out.empty()) {
        std::fprintf(stderr, "usage: cook --content <dir> --out <content.pak>\n");
        return 1;
    }

    std::vector<engine::assets::PakEntry> entries;
    const bool ok = cook_mesh_obj(content / "meshes" / "cube.obj", "/content/meshes/cube.solc", entries)
        && cook_mesh_gltf(content / "meshes" / "cartoon_husky.gltf",
            "/content/meshes/cartoon_husky.solc", entries)
        && cook_image(content / "textures" / "albedo.png", "/content/textures/albedo.solc", entries)
        && cook_image(content / "textures" / "husky" / "Cartoon_Husky_Albedo1.png",
            "/content/textures/husky/Cartoon_Husky_Albedo1.solc", entries)
        && cook_beep(entries);

    std::vector<engine::u8> pak;
    if (!ok || !engine::assets::write_pak(entries, pak) || !write_bytes(out, pak)) {
        engine::log(engine::LogLevel::Error, engine::LogChannel::Assets,
            std::string("Cook failed to write ") + out.string());
        return 1;
    }

    char message[256];
    std::snprintf(message, sizeof(message), "Cooked %zu entries -> %s", entries.size(),
        out.string().c_str());
    engine::log(engine::LogLevel::Info, engine::LogChannel::Assets, message);
    return 0;
}
