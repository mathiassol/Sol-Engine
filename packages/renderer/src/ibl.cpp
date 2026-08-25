#include <engine/renderer/ibl.hpp>

#include <engine/math/constants.hpp>
#include <engine/renderer/pbr.hpp>

#include <cmath>
#include <cstdint>

namespace engine::renderer::ibl {
namespace {

constexpr u32 kIrradianceSamples = 32;
constexpr u32 kPrefilterSamples = 32;
constexpr u32 kLutSamples = 64;

math::Vec3 add(math::Vec3 a, math::Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

math::Vec3 scale(math::Vec3 a, f32 s) {
    return {a.x * s, a.y * s, a.z * s};
}

math::Vec3 lerp3(math::Vec3 a, math::Vec3 b, f32 t) {
    return add(scale(a, 1.f - t), scale(b, t));
}

u16 float_to_half(f32 value) {
    if (!std::isfinite(value)) {
        return value > 0.f ? 0x7c00 : 0xfc00;
    }
    if (value > 65504.f) {
        value = 65504.f;
    }
    if (value < -65504.f) {
        value = -65504.f;
    }
    union {
        f32 f;
        u32 u;
    } v{value};
    const u32 sign = (v.u >> 16) & 0x8000u;
    const i32 exp = static_cast<i32>((v.u >> 23) & 0xffu) - 127 + 15;
    const u32 mag = v.u & 0x7fffffu;
    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<u16>(sign);
        }
        const u32 m = (mag | 0x800000u) >> (1 - exp);
        return static_cast<u16>(sign | ((m + 0x1000u) >> 13));
    }
    if (exp >= 31) {
        return static_cast<u16>(sign | 0x7c00u);
    }
    return static_cast<u16>(sign | (static_cast<u32>(exp) << 10) | ((mag + 0x1000u) >> 13));
}

void pack_pixel(std::vector<u16>& out, math::Vec3 rgb) {
    out.push_back(float_to_half(rgb.x));
    out.push_back(float_to_half(rgb.y));
    out.push_back(float_to_half(rgb.z));
    out.push_back(float_to_half(1.f));
}

math::Vec3 cube_direction(u32 face, f32 u, f32 v) {
    const f32 su = u * 2.f - 1.f;
    const f32 sv = v * 2.f - 1.f;
    math::Vec3 dir{};
    switch (face) {
    case 0:
        dir = {1.f, -sv, -su};
        break;
    case 1:
        dir = {-1.f, -sv, su};
        break;
    case 2:
        dir = {su, 1.f, sv};
        break;
    case 3:
        dir = {su, -1.f, -sv};
        break;
    case 4:
        dir = {su, -sv, 1.f};
        break;
    default:
        dir = {-su, -sv, -1.f};
        break;
    }
    return dir.normalized();
}

void orthonormal_basis(math::Vec3 n, math::Vec3& t, math::Vec3& b) {
    const math::Vec3 up = std::abs(n.z) < 0.999f ? math::Vec3{0.f, 0.f, 1.f} : math::Vec3{1.f, 0.f, 0.f};
    t = n.cross(up).normalized();
    b = t.cross(n);
}

math::Vec3 world_from_tangent(math::Vec3 n, math::Vec3 local) {
    math::Vec3 t, b;
    orthonormal_basis(n, t, b);
    return add(add(scale(t, local.x), scale(b, local.y)), scale(n, local.z)).normalized();
}

f32 radical_inverse_vdC(u32 bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<f32>(bits) * 2.3283064365386963e-10f;
}

math::Vec2 hammersley(u32 i, u32 n) {
    return {static_cast<f32>(i) / static_cast<f32>(n), radical_inverse_vdC(i)};
}

math::Vec3 importance_sample_ggx(math::Vec2 xi, math::Vec3 n, f32 alpha) {
    const f32 phi = 2.f * math::kPi * xi.x;
    const f32 a2 = alpha * alpha;
    const f32 cos_theta = std::sqrt((1.f - xi.y) / (1.f + (a2 - 1.f) * xi.y));
    const f32 sin_theta = std::sqrt(std::max(0.f, 1.f - cos_theta * cos_theta));
    const math::Vec3 h_local{
        std::cos(phi) * sin_theta,
        std::sin(phi) * sin_theta,
        cos_theta,
    };
    return world_from_tangent(n, h_local);
}

f32 geometry_schlick_ggx_ibl(f32 nodot, f32 roughness) {
    const f32 k = (roughness * roughness) / 2.f;
    return nodot / (nodot * (1.f - k) + k);
}

f32 geometry_smith_ibl(f32 nov, f32 nol, f32 roughness) {
    return geometry_schlick_ggx_ibl(nov, roughness) * geometry_schlick_ggx_ibl(nol, roughness);
}

math::Vec3 sample_radiance(const std::vector<math::Vec3>& faces, u32 size, math::Vec3 dir) {
    const math::Vec3 absd{std::abs(dir.x), std::abs(dir.y), std::abs(dir.z)};
    u32 face = 4;
    f32 u = 0.f;
    f32 v = 0.f;
    if (absd.x >= absd.y && absd.x >= absd.z) {
        face = dir.x > 0.f ? 0u : 1u;
        const f32 inv = 1.f / absd.x;
        u = dir.x > 0.f ? (-dir.z * inv) : (dir.z * inv);
        v = -dir.y * inv;
    } else if (absd.y >= absd.z) {
        face = dir.y > 0.f ? 2u : 3u;
        const f32 inv = 1.f / absd.y;
        u = dir.x * inv;
        v = dir.y > 0.f ? (dir.z * inv) : (-dir.z * inv);
    } else {
        face = dir.z > 0.f ? 4u : 5u;
        const f32 inv = 1.f / absd.z;
        u = dir.z > 0.f ? (dir.x * inv) : (-dir.x * inv);
        v = -dir.y * inv;
    }
    const f32 su = (u * 0.5f + 0.5f) * static_cast<f32>(size - 1);
    const f32 sv = (v * 0.5f + 0.5f) * static_cast<f32>(size - 1);
    const u32 x = static_cast<u32>(std::min(su, static_cast<f32>(size - 1)));
    const u32 y = static_cast<u32>(std::min(sv, static_cast<f32>(size - 1)));
    return faces[face * size * size + y * size + x];
}

std::vector<math::Vec3> bake_radiance() {
    std::vector<math::Vec3> faces(6u * kRadianceSize * kRadianceSize);
    for (u32 face = 0; face < 6; ++face) {
        for (u32 y = 0; y < kRadianceSize; ++y) {
            for (u32 x = 0; x < kRadianceSize; ++x) {
                const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(kRadianceSize);
                const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kRadianceSize);
                faces[face * kRadianceSize * kRadianceSize + y * kRadianceSize + x] =
                    sky_radiance(cube_direction(face, u, v));
            }
        }
    }
    return faces;
}

} // namespace

math::Vec3 sky_radiance(math::Vec3 direction) {
    const math::Vec3 dir = direction.normalized();
    // Clear day, not overcast. No sun disk here — that is skybox-only so IBL
    // does not double-count the directional.
    const math::Vec3 zenith{0.16f, 0.38f, 0.82f};
    const math::Vec3 horizon{0.70f, 0.78f, 0.92f};
    const math::Vec3 ground{0.08f, 0.075f, 0.06f};
    if (dir.y >= 0.f) {
        const f32 t = std::pow(dir.y, 0.35f);
        return lerp3(horizon, zenith, t);
    }
    const f32 t = std::min(1.f, -dir.y);
    return lerp3(horizon, ground, t);
}

f32 lod_from_roughness(f32 perceptual_roughness) {
    const f32 r = pbr::saturate(perceptual_roughness);
    return r * kMaxLod;
}

math::Vec2 integrate_brdf(f32 nov, f32 perceptual_roughness, u32 sample_count) {
    nov = std::max(nov, 1e-4f);
    const math::Vec3 n{0.f, 0.f, 1.f};
    const math::Vec3 v{std::sqrt(std::max(0.f, 1.f - nov * nov)), 0.f, nov};
    const f32 alpha = perceptual_roughness * perceptual_roughness;
    f32 a = 0.f;
    f32 b = 0.f;
    for (u32 i = 0; i < sample_count; ++i) {
        const math::Vec2 xi = hammersley(i, sample_count);
        const math::Vec3 h = importance_sample_ggx(xi, n, alpha);
        const math::Vec3 l = (h * (2.f * v.dot(h)) - v).normalized();
        const f32 nol = pbr::saturate(l.z);
        const f32 noh = pbr::saturate(h.z);
        const f32 voh = pbr::saturate(v.dot(h));
        if (nol <= 0.f) {
            continue;
        }
        const f32 g = geometry_smith_ibl(nov, nol, perceptual_roughness);
        const f32 g_vis = (g * voh) / std::max(noh * nov, 1e-5f);
        const f32 fc = std::pow(1.f - voh, 5.f);
        a += (1.f - fc) * g_vis;
        b += fc * g_vis;
    }
    const f32 inv = 1.f / static_cast<f32>(sample_count);
    return {a * inv, b * inv};
}

Baked bake() {
    const std::vector<math::Vec3> radiance = bake_radiance();
    Baked out{};

    out.source_rgba16.reserve(6u * kSourceSize * kSourceSize * 4u);
    for (u32 face = 0; face < 6; ++face) {
        for (u32 y = 0; y < kSourceSize; ++y) {
            for (u32 x = 0; x < kSourceSize; ++x) {
                const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(kSourceSize);
                const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kSourceSize);
                pack_pixel(out.source_rgba16, sky_radiance(cube_direction(face, u, v)));
            }
        }
    }

    out.irradiance_rgba16.reserve(6u * kIrradianceSize * kIrradianceSize * 4u);
    for (u32 face = 0; face < 6; ++face) {
        for (u32 y = 0; y < kIrradianceSize; ++y) {
            for (u32 x = 0; x < kIrradianceSize; ++x) {
                const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(kIrradianceSize);
                const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kIrradianceSize);
                const math::Vec3 n = cube_direction(face, u, v);
                math::Vec3 acc{};
                for (u32 i = 0; i < kIrradianceSamples; ++i) {
                    const math::Vec2 xi = hammersley(i, kIrradianceSamples);
                    const f32 r = std::sqrt(xi.x);
                    const f32 phi = 2.f * math::kPi * xi.y;
                    const math::Vec3 local{r * std::cos(phi), r * std::sin(phi),
                        std::sqrt(std::max(0.f, 1.f - xi.x))};
                    const math::Vec3 l = world_from_tangent(n, local);
                    acc = add(acc, sample_radiance(radiance, kRadianceSize, l));
                }
                pack_pixel(out.irradiance_rgba16, scale(acc, math::kPi / static_cast<f32>(kIrradianceSamples)));
            }
        }
    }

    u32 mip_texels = 0;
    u32 mip_w = kPrefilterSize;
    for (u32 mip = 0; mip < kPrefilterMips; ++mip) {
        mip_texels += mip_w * mip_w;
        mip_w = mip_w > 1 ? mip_w / 2 : 1;
    }
    out.prefilter_rgba16.reserve(6u * mip_texels * 4u);
    for (u32 face = 0; face < 6; ++face) {
        u32 size = kPrefilterSize;
        for (u32 mip = 0; mip < kPrefilterMips; ++mip) {
            const f32 roughness = static_cast<f32>(mip) / kMaxLod;
            const f32 alpha = roughness * roughness;
            for (u32 y = 0; y < size; ++y) {
                for (u32 x = 0; x < size; ++x) {
                    const f32 u = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size);
                    const f32 v = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size);
                    const math::Vec3 n = cube_direction(face, u, v);
                    const math::Vec3 view = n;
                    math::Vec3 acc{};
                    f32 weight = 0.f;
                    const u32 samples = roughness < 0.01f ? 1u : kPrefilterSamples;
                    for (u32 i = 0; i < samples; ++i) {
                        const math::Vec2 xi = hammersley(i, samples);
                        const math::Vec3 h = importance_sample_ggx(xi, n, std::max(alpha, 0.001f));
                        const math::Vec3 l = (h * (2.f * view.dot(h)) - view).normalized();
                        const f32 nol = pbr::saturate(n.dot(l));
                        if (nol <= 0.f) {
                            continue;
                        }
                        acc = add(acc, scale(sample_radiance(radiance, kRadianceSize, l), nol));
                        weight += nol;
                    }
                    pack_pixel(out.prefilter_rgba16, weight > 0.f ? scale(acc, 1.f / weight) : math::Vec3{});
                }
            }
            size = size > 1 ? size / 2 : 1;
        }
    }

    out.lut_rgba16.reserve(kLutSize * kLutSize * 4u);
    for (u32 y = 0; y < kLutSize; ++y) {
        for (u32 x = 0; x < kLutSize; ++x) {
            const f32 nov = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(kLutSize);
            const f32 roughness = (static_cast<f32>(y) + 0.5f) / static_cast<f32>(kLutSize);
            const math::Vec2 dfg = integrate_brdf(nov, roughness, kLutSamples);
            pack_pixel(out.lut_rgba16, {dfg.x, dfg.y, 0.f});
        }
    }
    return out;
}

} // namespace engine::renderer::ibl
