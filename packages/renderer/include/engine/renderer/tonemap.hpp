#pragma once

#include <engine/core/types.hpp>
#include <engine/math/vec4.hpp>
#include <engine/renderer/bloom.hpp>

namespace engine::renderer::tonemap {

// Keep in sync with tonemap.hlsl.
//
// The tonemap pass had no constant buffer at all until exposure needed one - it
// was the only post pass with zero parameters. Two values ride in it:
//
//   .x  exposure, a linear multiplier (2^ev; the app owns the EV convention)
//   .y  bloom intensity
//
// bloom_intensity is here because it was already duplicated - bloom::kIntensity
// in C++ against a `static const float kBloomIntensity` in tonemap.hlsl, with
// nothing keeping them equal. The buffer has to exist for exposure regardless,
// so routing the existing constant through it costs nothing and removes a live
// drift risk.
struct Constants {
    math::Vec4 params{};
};

static_assert(sizeof(Constants) == 16);

inline Constants make_constants(f32 exposure) {
    Constants out{};
    out.params = {exposure, bloom::kIntensity, 0.f, 0.f};
    return out;
}

} // namespace engine::renderer::tonemap
