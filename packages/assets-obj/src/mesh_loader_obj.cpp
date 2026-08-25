#include <engine/assets/obj/mesh_loader_obj.hpp>

#include <engine/core/log.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets::obj {

using engine::LogChannel;
using engine::LogLevel;

namespace {

struct Position {
    f32 x = 0.f;
    f32 y = 0.f;
    f32 z = 0.f;
};

struct TexCoord {
    f32 u = 0.f;
    f32 v = 0.f;
};

struct FaceKey {
    i32 vi = 0;
    i32 vti = 0;
    i32 vni = 0;

    bool operator==(const FaceKey& o) const {
        return vi == o.vi && vti == o.vti && vni == o.vni;
    }
};

struct FaceKeyHash {
    usize operator()(const FaceKey& k) const {
        return static_cast<usize>(k.vi) * 73856093u
             ^ static_cast<usize>(k.vti) * 19349663u
             ^ static_cast<usize>(k.vni) * 83492791u;
    }
};

void parse_face_corner(std::string_view corner, i32& vi, i32& vti, i32& vni) {
    vi = vti = vni = 0;
    const usize slash1 = corner.find('/');
    if (slash1 == std::string_view::npos) {
        vi = std::stoi(std::string(corner));
        return;
    }

    vi = std::stoi(std::string(corner.substr(0, slash1)));
    const usize slash2 = corner.find('/', slash1 + 1);
    if (slash2 == slash1 + 1) {
        vni = std::stoi(std::string(corner.substr(slash2 + 1)));
        return;
    }
    if (slash2 != std::string_view::npos) {
        vti = std::stoi(std::string(corner.substr(slash1 + 1, slash2 - slash1 - 1)));
        vni = std::stoi(std::string(corner.substr(slash2 + 1)));
        return;
    }
    vti = std::stoi(std::string(corner.substr(slash1 + 1)));
}

VertexPN make_vertex(const std::vector<Position>& positions,
    const std::vector<TexCoord>& uvs, const std::vector<Position>& normals,
    i32 vi, i32 vti, i32 vni) {
    VertexPN v{};
    if (vi >= 0 && static_cast<usize>(vi) < positions.size()) {
        v.px = positions[static_cast<usize>(vi)].x;
        v.py = positions[static_cast<usize>(vi)].y;
        v.pz = positions[static_cast<usize>(vi)].z;
    }
    if (vti >= 0 && static_cast<usize>(vti) < uvs.size()) {
        v.u = uvs[static_cast<usize>(vti)].u;
        v.v = uvs[static_cast<usize>(vti)].v;
    }
    if (vni >= 0 && static_cast<usize>(vni) < normals.size()) {
        v.nx = normals[static_cast<usize>(vni)].x;
        v.ny = normals[static_cast<usize>(vni)].y;
        v.nz = normals[static_cast<usize>(vni)].z;
    } else {
        v.ny = 1.f;
    }
    return v;
}

class ObjMeshLoader final : public IMeshLoader {
public:
    bool load(std::string_view path, MeshData& out) override {
        std::ifstream file{std::string(path)};
        if (!file) {
            engine::log(LogLevel::Error, LogChannel::Assets, "Failed to open mesh file");
            return false;
        }

        std::vector<Position> positions;
        std::vector<TexCoord> uvs;
        std::vector<Position> normals;
        std::unordered_map<FaceKey, u32, FaceKeyHash> dedupe;

        out.vertices.clear();
        out.indices.clear();

        auto resolve_index = [](i32 index, usize count) -> i32 {
            if (index > 0) return index - 1;
            if (index < 0) return static_cast<i32>(count) + index;
            return -1;
        };

        auto add_corner = [&](std::string_view corner) {
            i32 vi = 0, vti = 0, vni = 0;
            parse_face_corner(corner, vi, vti, vni);

            const i32 vi0 = resolve_index(vi, positions.size());
            const i32 vti0 = resolve_index(vti, uvs.size());
            const i32 vni0 = resolve_index(vni, normals.size());

            FaceKey key{vi0, vti0, vni0};
            auto [it, inserted] = dedupe.emplace(key, static_cast<u32>(out.vertices.size()));
            if (inserted) {
                out.vertices.push_back(make_vertex(positions, uvs, normals, vi0, vti0, vni0));
            }
            out.indices.push_back(it->second);
        };

        std::string line;
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;

            std::istringstream ss(line);
            std::string tag;
            ss >> tag;

            if (tag == "v") {
                Position p{};
                ss >> p.x >> p.y >> p.z;
                positions.push_back(p);
            } else if (tag == "vt") {
                TexCoord t{};
                ss >> t.u >> t.v;
                uvs.push_back(t);
            } else if (tag == "vn") {
                Position n{};
                ss >> n.x >> n.y >> n.z;
                normals.push_back(n);
            } else if (tag == "f") {
                std::vector<std::string> corners;
                std::string corner;
                while (ss >> corner) {
                    corners.push_back(corner);
                }
                if (corners.size() < 3) continue;

                for (usize i = 1; i + 1 < corners.size(); ++i) {
                    add_corner(corners[0]);
                    add_corner(corners[i]);
                    add_corner(corners[i + 1]);
                }
            }
        }

        if (out.vertices.empty() || out.indices.empty()) {
            engine::log(LogLevel::Error, LogChannel::Assets, "Mesh file contained no geometry");
            return false;
        }

        compute_mesh_bounds(out);
        return true;
    }
};

} // namespace

std::unique_ptr<IMeshLoader> create_mesh_loader() {
    return std::make_unique<ObjMeshLoader>();
}

} // namespace engine::assets::obj
