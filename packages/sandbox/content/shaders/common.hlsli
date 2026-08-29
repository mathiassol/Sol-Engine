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

// Clip-space XY (already divided by w) -> UV.
float2 ndc_to_uv(float2 ndc) {
    return ndc * float2(0.5, -0.5) + 0.5;
}

// UV -> clip-space XY.
float2 uv_to_ndc(float2 uv) {
    return uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
}

#endif // SOL_COMMON_HLSLI
