// The one shader both backends draw, so a divergence between them cannot be a
// difference in what they were asked to draw.
//
// A cbuffer at b0 on purpose. Without one this gate would exercise no
// descriptor plumbing at all, and translating resources.hpp's five counts into
// a descriptor-set layout is exactly what a second backend has to get right.
// Under SPIR-V, b0 lands at set 0 binding 0 - see the register shifts in
// shaders-dxc, which exist because the default HLSL mapping ignores the
// register type and would otherwise collide b0 with t0.
cbuffer ParityConstants : register(b0) {
    // Each channel is exactly representable in UNORM8 - 51/255, 153/255,
    // 204/255 - so the readback compares exact bytes instead of a tolerance,
    // and no channel sits on a .5 rounding tie.
    float4 tint;
};

struct VSOutput {
    float4 pos : SV_POSITION;
};

VSOutput vs_main(uint id : SV_VertexID) {
    // Clip space directly: no vertex buffer and no input layout, so the gate
    // covers one thing at a time. Top-left, top-right, bottom-left - the
    // upper-left half of the target, with the hypotenuse corner to corner.
    //
    // Which half it is matters. A backend that gets the Y direction wrong draws
    // the *lower-right* half, and the two probe texels the gate reads are
    // placed as mirrors across that diagonal to catch exactly this, rather
    // than averaging it away in a coverage count.
    const float2 corners[3] = {
        float2(-1.0,  1.0),
        float2( 1.0,  1.0),
        float2(-1.0, -1.0),
    };
    VSOutput output;
    output.pos = float4(corners[id], 0.0, 1.0);
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET {
    return tint;
}
