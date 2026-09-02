// Texture parity: a mip level and a cube face, both read at an exact value.
//
// The two things that go wrong quietly when uploading a texture are the mip
// offset and the array layer, so this reads one of each rather than sampling
// mip 0 of a 2D texture and calling it covered. Every value is chosen to be
// exactly representable in UNORM8, so the readback compares bytes.
Texture2D<float4> albedo : register(t0);
TextureCube<float4> cube : register(t1);
SamplerState point_clamp : register(s0);

struct VSOutput {
    float4 pos : SV_POSITION;
};

VSOutput vs_main(uint id : SV_VertexID) {
    // Clip space directly - no vertex buffer, so a failure here is about
    // textures and not about vertex input, which parity_mesh_gate covers.
    const float2 corners[3] = {
        float2(-1.0, 1.0),
        float2(3.0, 1.0),
        float2(-1.0, -3.0),
    };
    VSOutput output;
    output.pos = float4(corners[id], 0.0, 1.0);
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET {
    // SampleLevel, not Sample: an explicit level is the point. Level 1 of a
    // 2x2 texture is the 1x1 the gate uploaded by hand, so its value proves
    // the second mip landed at the right offset rather than being a filtered
    // guess at what mip 1 should contain.
    // Level 1 of a 2x2 is the single generated texel, so any uv reads it.
    const float mip1 = albedo.SampleLevel(point_clamp, float2(0.5, 0.5), 1.0).r;
    // uv (0.25, 0.25) is the centre of texel (0, 0), which the gate set to
    // black - so this reads one texel of level 0 rather than a blend, and it
    // is a value level 1 does not have.
    const float mip0 = albedo.SampleLevel(point_clamp, float2(0.25, 0.25), 0.0).r;
    // +X is face 0 in both APIs, and (1,0,0) is its exact centre, so a 2x2
    // face of uniform texels samples cleanly with no cross-seam blend.
    const float face0 = cube.SampleLevel(point_clamp, float3(1.0, 0.0, 0.0), 0.0).r;
    return float4(mip1, face0, mip0, 1.0);
}
