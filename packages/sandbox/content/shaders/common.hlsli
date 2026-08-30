#ifndef SOL_COMMON_HLSLI
#define SOL_COMMON_HLSLI

// Shared shader conventions.
//
// Four conventions decide whether a shader is portable. Three of them already
// hold for D3D12, Vulkan and Metal alike:
//
//   depth range      [0, 1]
//   matrix storage   column-major, applied as mul(M, v)
//   texture origin   UV (0,0) is top-left
//
// The fourth, **NDC Y direction**, is API-specific: D3D12 and Metal agree,
// Vulkan is flipped. Every helper below that touches Y lives here so that flip
// is one edit, not twelve. It used to be copy-pasted into ten fullscreen
// vertex shaders plus two hand-rolled conversions.
//
// A Vulkan backend has two options and they are not interchangeable:
//   * negative-height viewport - fixes everything that goes through
//     SV_Position, but NOT values carried as interpolated varyings, and it
//     reverses triangle winding so CullMode must be swapped;
//   * flip in the projection matrix - fixes varyings, breaks SV_Position users.
// motion.hlsl and sky.hlsl pass clip space through varyings, so whichever is
// chosen, audit those two.

// Fullscreen triangle from a vertex id, no vertex buffer. Three vertices give
// UVs (0,0), (2,0), (0,2), which covers the screen with one triangle rather
// than two - no diagonal seam, and the GPU clips the overhang for free.
float2 fullscreen_uv(uint vertex_id) {
    return float2((vertex_id << 1) & 2, vertex_id & 2);
}

// UV -> clip position. The negative Y scale is the D3D/Metal convention.
float4 fullscreen_position(float2 uv, float depth) {
    return float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), depth, 1.0);
}

float4 fullscreen_position(float2 uv) {
    return fullscreen_position(uv, 0.0);
}

// ── Colour space ─────────────────────────────────────────────────────────────
//
// Not a portability convention like the four above — this one is a correctness
// convention, and the engine got it wrong for its whole life. 8-bit colour is
// stored sRGB-encoded, lighting maths is linear, and the encode is re-applied
// exactly once at the very end of the frame.
//
// Hardware sRGB texture formats cover the decode on the way in, before
// filtering. Nothing covers the encode on the way out, because the swapchain is
// UNORM (flip-model swapchains do not accept _SRGB formats), so the tonemap
// shaders call srgb_encode as their final operation.
//
// Piecewise, not pow(x, 1/2.2): the curve is linear below 0.0031308, and a pow
// approximation is over 50% too bright at linear 0.001 — exactly where shadow
// detail lives. Must match engine::math::linear_to_srgb / srgb_to_linear in
// packages/math/include/engine/math/srgb.hpp; srgb_gate.hlsl reads both back
// through a compute dispatch so they cannot drift apart.
// `linear` is an HLSL interpolation modifier, so it cannot name a parameter.
float srgb_encode(float lin) {
    float c = saturate(lin);
    return c <= 0.0031308 ? c * 12.92 : 1.055 * pow(c, 1.0 / 2.4) - 0.055;
}

float3 srgb_encode(float3 lin) {
    return float3(srgb_encode(lin.r), srgb_encode(lin.g), srgb_encode(lin.b));
}

float srgb_decode(float encoded) {
    float c = saturate(encoded);
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

float3 srgb_decode(float3 encoded) {
    return float3(srgb_decode(encoded.r), srgb_decode(encoded.g), srgb_decode(encoded.b));
}

// Clip-space XY (already divided by w) -> UV.
float2 ndc_to_uv(float2 ndc) {
    return ndc * float2(0.5, -0.5) + 0.5;
}

// UV -> clip-space XY.
float2 uv_to_ndc(float2 uv) {
    return uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
}

#endif // SOL_COMMON_HLSLI
