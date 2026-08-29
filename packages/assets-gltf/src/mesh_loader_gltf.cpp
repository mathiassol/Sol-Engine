#include <engine/assets/gltf/mesh_loader_gltf.hpp>

#include <engine/core/log.hpp>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#define CGLTF_IMPLEMENTATION
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
#include "cgltf.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace engine::assets::gltf {

namespace {

const cgltf_accessor* find_attribute(const cgltf_primitive& prim, cgltf_attribute_type type,
    cgltf_int index = 0) {
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        if (prim.attributes[i].type != type) {
            continue;
        }
        if (type == cgltf_attribute_type_texcoord && prim.attributes[i].index != index) {
            continue;
        }
        return prim.attributes[i].data;
    }
    return nullptr;
}

bool unpack_vec(const cgltf_accessor* accessor, u32 components, std::vector<f32>& out) {
    if (!accessor || accessor->count == 0) {
        return false;
    }
    out.resize(static_cast<usize>(accessor->count) * components);
    const cgltf_size written = cgltf_accessor_unpack_floats(accessor, out.data(), out.size());
    return written == out.size();
}

std::string image_uri(const cgltf_texture_view& view, const std::filesystem::path& gltf_dir) {
    if (!view.texture || !view.texture->image || !view.texture->image->uri) {
        return {};
    }
    return (gltf_dir / view.texture->image->uri).lexically_normal().string();
}

void fill_primitive_material(const cgltf_primitive& prim, const std::filesystem::path& gltf_dir,
    GltfPrimitive& out) {
    out.metallic = 0.f;
    out.roughness = 1.f;
    if (!prim.material) {
        return;
    }
    if (prim.material->has_pbr_metallic_roughness) {
        const cgltf_pbr_metallic_roughness& pbr = prim.material->pbr_metallic_roughness;
        out.metallic = static_cast<engine::f32>(pbr.metallic_factor);
        out.roughness = static_cast<engine::f32>(pbr.roughness_factor);
        out.albedo_uri = image_uri(pbr.base_color_texture, gltf_dir);
        out.metallic_roughness_uri = image_uri(pbr.metallic_roughness_texture, gltf_dir);
    }
    out.normal_uri = image_uri(prim.material->normal_texture, gltf_dir);
}

bool append_primitive(const cgltf_primitive& prim, MeshData& mesh) {
    if (prim.type != cgltf_primitive_type_triangles) {
        return false;
    }

    const cgltf_accessor* positions = find_attribute(prim, cgltf_attribute_type_position);
    const cgltf_accessor* normals = find_attribute(prim, cgltf_attribute_type_normal);
    const cgltf_accessor* uvs = find_attribute(prim, cgltf_attribute_type_texcoord, 0);
    if (!positions) {
        engine::log(LogLevel::Error, LogChannel::Assets, "glTF primitive has no POSITION");
        return false;
    }

    std::vector<f32> pos_f;
    std::vector<f32> nrm_f;
    std::vector<f32> uv_f;
    if (!unpack_vec(positions, 3, pos_f)) {
        engine::log(LogLevel::Error, LogChannel::Assets, "Failed to unpack glTF positions");
        return false;
    }
    if (normals) {
        unpack_vec(normals, 3, nrm_f);
    }
    if (uvs) {
        unpack_vec(uvs, 2, uv_f);
    }

    const u32 vertex_base = static_cast<u32>(mesh.vertices.size());
    const u32 vertex_count = static_cast<u32>(positions->count);
    mesh.vertices.resize(vertex_base + vertex_count);
    for (u32 i = 0; i < vertex_count; ++i) {
        VertexPN& v = mesh.vertices[vertex_base + i];
        v.px = pos_f[i * 3 + 0];
        v.py = pos_f[i * 3 + 1];
        v.pz = pos_f[i * 3 + 2];
        if (nrm_f.size() >= (i + 1) * 3) {
            v.nx = nrm_f[i * 3 + 0];
            v.ny = nrm_f[i * 3 + 1];
            v.nz = nrm_f[i * 3 + 2];
        } else {
            v.ny = 1.f;
        }
        if (uv_f.size() >= (i + 1) * 2) {
            v.u = uv_f[i * 2 + 0];
            v.v = uv_f[i * 2 + 1];
        }
    }

    if (prim.indices) {
        std::vector<u32> indices(static_cast<usize>(prim.indices->count));
        const cgltf_size written = cgltf_accessor_unpack_indices(
            prim.indices, indices.data(), sizeof(u32), indices.size());
        if (written != indices.size()) {
            engine::log(LogLevel::Error, LogChannel::Assets, "Failed to unpack glTF indices");
            return false;
        }
        mesh.indices.reserve(mesh.indices.size() + indices.size());
        for (u32 index : indices) {
            mesh.indices.push_back(vertex_base + index);
        }
    } else {
        mesh.indices.reserve(mesh.indices.size() + vertex_count);
        for (u32 i = 0; i < vertex_count; ++i) {
            mesh.indices.push_back(vertex_base + i);
        }
    }
    return true;
}

class GltfMeshLoader final : public IGltfLoader {
public:
    bool load(std::string_view path, GltfLoadResult& out) override {
        out = {};
        const std::string file{path};
        cgltf_options options{};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, file.c_str(), &data);
        if (result != cgltf_result_success || !data) {
            engine::log(LogLevel::Error, LogChannel::Assets, "Failed to parse glTF");
            return false;
        }
        result = cgltf_load_buffers(&options, data, file.c_str());
        if (result != cgltf_result_success) {
            engine::log(LogLevel::Error, LogChannel::Assets, "Failed to load glTF buffers");
            cgltf_free(data);
            return false;
        }

        // Not optional. cgltf's accessor unpack functions assume validated data
        // — this is the only place accessor ranges, buffer-view bounds and
        // sparse indices are checked against the actual buffer sizes. Without
        // it, a file whose accessor count exceeds its buffer view reads past
        // the allocation, and a sparse index writes past it.
        result = cgltf_validate(data);
        if (result != cgltf_result_success) {
            char message[96];
            std::snprintf(message, sizeof(message),
                "glTF failed validation (cgltf_result %d) — refusing to load", static_cast<int>(result));
            engine::log(LogLevel::Error, LogChannel::Assets, message);
            cgltf_free(data);
            return false;
        }

        const std::filesystem::path gltf_dir = std::filesystem::path(file).parent_path();
        for (cgltf_size m = 0; m < data->meshes_count; ++m) {
            const cgltf_mesh& mesh = data->meshes[m];
            for (cgltf_size p = 0; p < mesh.primitives_count; ++p) {
                const cgltf_primitive& prim = mesh.primitives[p];
                if (prim.type != cgltf_primitive_type_triangles) {
                    continue;
                }
                const u32 first_index = static_cast<u32>(out.mesh.indices.size());
                if (!append_primitive(prim, out.mesh)) {
                    cgltf_free(data);
                    return false;
                }
                GltfPrimitive loaded{};
                loaded.first_index = first_index;
                loaded.index_count = static_cast<u32>(out.mesh.indices.size()) - first_index;
                fill_primitive_material(prim, gltf_dir, loaded);
                out.primitives.push_back(std::move(loaded));
            }
        }

        cgltf_free(data);
        if (out.primitives.empty() || out.mesh.vertices.empty() || out.mesh.indices.empty()) {
            engine::log(LogLevel::Error, LogChannel::Assets, "glTF contained no triangle primitives");
            return false;
        }
        out.albedo_uri = out.primitives[0].albedo_uri;
        out.metallic = out.primitives[0].metallic;
        out.roughness = out.primitives[0].roughness;
        compute_mesh_bounds(out.mesh);
        return true;
    }
};

} // namespace

std::unique_ptr<IGltfLoader> create_mesh_loader() {
    return std::make_unique<GltfMeshLoader>();
}

} // namespace engine::assets::gltf
