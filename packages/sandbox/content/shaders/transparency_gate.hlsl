// Gate-only. Draws an axis-aligned quad in a flat colour, both taken from the
// cbuffer, so one shader covers the opaque underlay and the blended overlay by
// changing constants rather than by having two shaders that could differ.
//
// Deliberately not forward.hlsl: that wants seven textures, three samplers, a
// structured instance buffer and an IBL set, none of which says anything about
// blending. What this gate measures is the blend, so everything else is noise.
// It also targets RGBA16_FLOAT, where a readback comparison has to reason
// about float rounding instead of about the blend.

cbuffer Params : register(b0) {
    // Quad extents in NDC: (min_x, min_y, max_x, max_y).
    float4 rect;
    // Returned verbatim as SV_TARGET, alpha included. Under BlendMode::Alpha
    // that alpha is the blend factor.
    float4 tint;
};

struct PSInput {
    float4 pos : SV_POSITION;
};

PSInput vs_main(uint id : SV_VertexID) {
    // Two triangles as a quad, indexed off SV_VertexID so the gate needs no
    // vertex buffer and no index buffer at all.
    const float2 corners[6] = {
        float2(0, 0), float2(1, 0), float2(0, 1),
        float2(0, 1), float2(1, 0), float2(1, 1),
    };
    const float2 c = corners[id];
    PSInput output;
    output.pos = float4(lerp(rect.x, rect.z, c.x), lerp(rect.y, rect.w, c.y), 0.0, 1.0);
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET {
    return tint;
}
