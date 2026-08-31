#include <engine/assets/cooked.hpp>

#include <engine/core/log.hpp>

#include <cstdio>
#include <cstring>

namespace engine::assets {
namespace {

constexpr u8 kMagic[4] = {'S', 'O', 'L', 'C'};

// A cooked blob is produced by the cooker, not hand-written, so the useful
// diagnostic is which read failed and on what - a truncated or stale .solc
// shipped to a player is otherwise a silent "asset just did not load". One
// message per failed public read; the per-field Cursor checks stay quiet.
bool reject(const char* what, std::span<const u8> bytes, const char* reason) {
    char message[192];
    std::snprintf(message, sizeof(message), "SOLC %s rejected (%zu bytes): %s",
        what, bytes.size(), reason);
    log(LogLevel::Error, LogChannel::Assets, message);
    return false;
}

void append_u16_le(std::vector<u8>& out, u16 value) {
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
}

void append_u32_le(std::vector<u8>& out, u32 value) {
    out.push_back(static_cast<u8>(value));
    out.push_back(static_cast<u8>(value >> 8));
    out.push_back(static_cast<u8>(value >> 16));
    out.push_back(static_cast<u8>(value >> 24));
}

void append_f32_le(std::vector<u8>& out, f32 value) {
    u32 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32_le(out, bits);
}

void write_header(std::vector<u8>& out, CookedKind kind) {
    out.insert(out.end(), kMagic, kMagic + 4);
    append_u32_le(out, kCookedVersion);
    append_u32_le(out, static_cast<u32>(kind));
}

struct Cursor {
    std::span<const u8> bytes;
    usize pos = 0;

    bool remaining(usize n) const {
        return pos <= bytes.size() && n <= bytes.size() - pos;
    }

    bool consume(usize n) {
        if (!remaining(n)) {
            return false;
        }
        pos += n;
        return true;
    }

    bool read_u16(u16& value) {
        if (!remaining(2)) {
            return false;
        }
        value = static_cast<u16>(bytes[pos] | (static_cast<u16>(bytes[pos + 1]) << 8));
        pos += 2;
        return true;
    }

    bool read_u32(u32& value) {
        if (!remaining(4)) {
            return false;
        }
        value = static_cast<u32>(bytes[pos])
            | (static_cast<u32>(bytes[pos + 1]) << 8)
            | (static_cast<u32>(bytes[pos + 2]) << 16)
            | (static_cast<u32>(bytes[pos + 3]) << 24);
        pos += 4;
        return true;
    }

    bool read_f32(f32& value) {
        u32 bits = 0;
        if (!read_u32(bits)) {
            return false;
        }
        std::memcpy(&value, &bits, sizeof(value));
        return true;
    }

    bool exact_end() const { return pos == bytes.size(); }
};

bool read_header(Cursor& cur, CookedKind expected) {
    if (!cur.remaining(12)) {
        return false;
    }
    if (std::memcmp(cur.bytes.data() + cur.pos, kMagic, 4) != 0) {
        return false;
    }
    cur.pos += 4;
    u32 version = 0;
    u32 kind = 0;
    if (!cur.read_u32(version) || !cur.read_u32(kind)) {
        return false;
    }
    return version == kCookedVersion && kind == static_cast<u32>(expected);
}

bool valid_mesh(const MeshData& mesh) {
    if (mesh.vertices.empty() || mesh.indices.size() < 3 || (mesh.indices.size() % 3) != 0) {
        return false;
    }
    if (mesh.vertices.size() > 0xffffffffu || mesh.indices.size() > 0xffffffffu) {
        return false;
    }
    const u32 vertex_count = static_cast<u32>(mesh.vertices.size());
    if (static_cast<usize>(vertex_count) != mesh.vertices.size()) {
        return false;
    }
    for (u32 index : mesh.indices) {
        if (index >= vertex_count) {
            return false;
        }
    }
    return true;
}

bool valid_image(const ImageData& image) {
    if (image.width == 0 || image.height == 0) {
        return false;
    }
    const u64 bytes = static_cast<u64>(image.width) * static_cast<u64>(image.height) * 4ull;
    return bytes == static_cast<u64>(image.rgba.size());
}

bool valid_audio(const CookedAudio& audio) {
    if (audio.sample_rate == 0 || audio.pcm.empty() || audio.pcm.size() > 0xffffffffu) {
        return false;
    }
    if (audio.channels != 1 && audio.channels != 2) {
        return false;
    }
    if (audio.bits_per_sample != 16) {
        return false;
    }
    const usize frame = static_cast<usize>(audio.channels) * 2u;
    return (audio.pcm.size() % frame) == 0;
}

void write_vertex(std::vector<u8>& out, const VertexPN& vertex) {
    append_f32_le(out, vertex.px);
    append_f32_le(out, vertex.py);
    append_f32_le(out, vertex.pz);
    append_f32_le(out, vertex.nx);
    append_f32_le(out, vertex.ny);
    append_f32_le(out, vertex.nz);
    append_f32_le(out, vertex.u);
    append_f32_le(out, vertex.v);
}

bool read_vertex(Cursor& cur, VertexPN& vertex) {
    return cur.read_f32(vertex.px) && cur.read_f32(vertex.py) && cur.read_f32(vertex.pz)
        && cur.read_f32(vertex.nx) && cur.read_f32(vertex.ny) && cur.read_f32(vertex.nz)
        && cur.read_f32(vertex.u) && cur.read_f32(vertex.v);
}

} // namespace

bool peek_cooked_kind(std::span<const u8> bytes, CookedKind& out) {
    if (bytes.size() < 12) {
        return false;
    }
    if (std::memcmp(bytes.data(), kMagic, 4) != 0) {
        return false;
    }
    Cursor cur{bytes, 4};
    u32 version = 0;
    u32 kind = 0;
    if (!cur.read_u32(version) || !cur.read_u32(kind) || version != kCookedVersion) {
        return false;
    }
    if (kind != static_cast<u32>(CookedKind::Mesh)
        && kind != static_cast<u32>(CookedKind::Image)
        && kind != static_cast<u32>(CookedKind::Audio)) {
        return false;
    }
    out = static_cast<CookedKind>(kind);
    return true;
}

bool write_cooked_mesh(const MeshData& mesh, std::vector<u8>& out) {
    if (!valid_mesh(mesh)) {
        return false;
    }
    MeshData packed = mesh;
    if (!packed.bounds.valid()) {
        compute_mesh_bounds(packed);
    }

    std::vector<u8> blob;
    write_header(blob, CookedKind::Mesh);
    append_u32_le(blob, static_cast<u32>(packed.vertices.size()));
    append_u32_le(blob, static_cast<u32>(packed.indices.size()));
    append_f32_le(blob, packed.bounds.min.x);
    append_f32_le(blob, packed.bounds.min.y);
    append_f32_le(blob, packed.bounds.min.z);
    append_f32_le(blob, packed.bounds.max.x);
    append_f32_le(blob, packed.bounds.max.y);
    append_f32_le(blob, packed.bounds.max.z);
    for (const VertexPN& vertex : packed.vertices) {
        write_vertex(blob, vertex);
    }
    for (u32 index : packed.indices) {
        append_u32_le(blob, index);
    }
    out = std::move(blob);
    return true;
}

bool read_cooked_mesh(std::span<const u8> bytes, MeshData& out) {
    Cursor cur{bytes};
    if (!read_header(cur, CookedKind::Mesh)) {
        return reject("mesh", bytes, "magic, version or kind is not a cooked mesh");
    }
    u32 vertex_count = 0;
    u32 index_count = 0;
    if (!cur.read_u32(vertex_count) || !cur.read_u32(index_count)) {
        return reject("mesh", bytes, "truncated before the vertex and index counts");
    }
    if (vertex_count == 0 || index_count < 3 || (index_count % 3) != 0) {
        return reject("mesh", bytes, "no vertices, or an index count that is not a positive multiple of 3");
    }
    const u64 vertex_bytes = static_cast<u64>(vertex_count) * 32ull;
    const u64 index_bytes = static_cast<u64>(index_count) * 4ull;
    if (vertex_bytes / 32ull != vertex_count || index_bytes / 4ull != index_count) {
        return reject("mesh", bytes, "declared counts overflow when sized in bytes");
    }
    const u64 need = 24ull + vertex_bytes + index_bytes;
    const u64 left = static_cast<u64>(cur.bytes.size() - cur.pos);
    if (need < 24ull || need > left) {
        return reject("mesh", bytes, "declared geometry does not fit the remaining bytes");
    }

    MeshData mesh{};
    if (!cur.read_f32(mesh.bounds.min.x) || !cur.read_f32(mesh.bounds.min.y)
        || !cur.read_f32(mesh.bounds.min.z) || !cur.read_f32(mesh.bounds.max.x)
        || !cur.read_f32(mesh.bounds.max.y) || !cur.read_f32(mesh.bounds.max.z)) {
        return reject("mesh", bytes, "truncated inside the bounding box");
    }
    mesh.vertices.resize(vertex_count);
    for (VertexPN& vertex : mesh.vertices) {
        if (!read_vertex(cur, vertex)) {
            return reject("mesh", bytes, "truncated inside the vertex array");
        }
    }
    mesh.indices.resize(index_count);
    for (u32& index : mesh.indices) {
        if (!cur.read_u32(index) || index >= vertex_count) {
            return reject("mesh", bytes, "index array truncated, or an index past the last vertex");
        }
    }
    if (!cur.exact_end() || !valid_mesh(mesh)) {
        return reject("mesh", bytes, "trailing bytes after the geometry, or bounds that fail validation");
    }
    out = std::move(mesh);
    return true;
}

bool write_cooked_image(const ImageData& image, std::vector<u8>& out) {
    if (!valid_image(image)) {
        return false;
    }
    std::vector<u8> blob;
    write_header(blob, CookedKind::Image);
    append_u32_le(blob, image.width);
    append_u32_le(blob, image.height);
    blob.insert(blob.end(), image.rgba.begin(), image.rgba.end());
    out = std::move(blob);
    return true;
}

bool read_cooked_image(std::span<const u8> bytes, ImageData& out) {
    Cursor cur{bytes};
    if (!read_header(cur, CookedKind::Image)) {
        return reject("image", bytes, "magic, version or kind is not a cooked image");
    }
    ImageData image{};
    if (!cur.read_u32(image.width) || !cur.read_u32(image.height)) {
        return reject("image", bytes, "truncated before width and height");
    }
    const u64 pixel_bytes = static_cast<u64>(image.width) * static_cast<u64>(image.height) * 4ull;
    if (image.width == 0 || image.height == 0
        || pixel_bytes / 4ull / image.width != image.height
        || !cur.remaining(static_cast<usize>(pixel_bytes))) {
        return reject("image", bytes, "declared dimensions do not fit the remaining bytes");
    }
    image.rgba.resize(static_cast<usize>(pixel_bytes));
    std::memcpy(image.rgba.data(), cur.bytes.data() + cur.pos, image.rgba.size());
    if (!cur.consume(image.rgba.size()) || !cur.exact_end() || !valid_image(image)) {
        return reject("image", bytes, "trailing bytes after the pixels, or dimensions that fail validation");
    }
    out = std::move(image);
    return true;
}

bool write_cooked_audio(const CookedAudio& audio, std::vector<u8>& out) {
    if (!valid_audio(audio)) {
        return false;
    }
    std::vector<u8> blob;
    write_header(blob, CookedKind::Audio);
    append_u32_le(blob, audio.sample_rate);
    append_u16_le(blob, audio.channels);
    append_u16_le(blob, audio.bits_per_sample);
    append_u32_le(blob, static_cast<u32>(audio.pcm.size()));
    blob.insert(blob.end(), audio.pcm.begin(), audio.pcm.end());
    out = std::move(blob);
    return true;
}

bool read_cooked_audio(std::span<const u8> bytes, CookedAudio& out) {
    Cursor cur{bytes};
    if (!read_header(cur, CookedKind::Audio)) {
        return reject("audio", bytes, "magic, version or kind is not cooked audio");
    }
    CookedAudio audio{};
    u32 byte_count = 0;
    if (!cur.read_u32(audio.sample_rate) || !cur.read_u16(audio.channels)
        || !cur.read_u16(audio.bits_per_sample) || !cur.read_u32(byte_count)) {
        return reject("audio", bytes, "truncated before the format fields");
    }
    if (!cur.remaining(byte_count)) {
        return reject("audio", bytes, "declared PCM byte count does not fit the remaining bytes");
    }
    audio.pcm.resize(byte_count);
    std::memcpy(audio.pcm.data(), cur.bytes.data() + cur.pos, byte_count);
    if (!cur.consume(byte_count) || !cur.exact_end() || !valid_audio(audio)) {
        return reject("audio", bytes, "trailing bytes after the PCM, or a format that fails validation");
    }
    out = std::move(audio);
    return true;
}

} // namespace engine::assets
