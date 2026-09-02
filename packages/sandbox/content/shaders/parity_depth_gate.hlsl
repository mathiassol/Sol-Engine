// Depth parity: the nearer fragment wins, under whichever convention the
// device declares.
//
// Drawn three times with different z and tint. The middle draw is the nearer
// one and must win; the third is at the same z as the first and must lose. So
// the probe distinguishes four failures that all look alike in a screenshot:
// no depth test at all (the last draw wins), an inverted compare (the first
// draw wins), a clear value that rejects everything (the clear colour wins),
// and depth writes disabled (the last draw wins again).
cbuffer DepthConstants : register(b0) {
    // .rgb tint, .a unused. Exactly representable in UNORM8.
    float4 tint;
    // .x is the clip-space z for this draw.
    float4 params;
};

struct VSOutput {
    float4 pos : SV_POSITION;
};

VSOutput vs_main(uint id : SV_VertexID) {
    // A full-target triangle, so every draw covers every texel and the only
    // thing deciding the result is the depth test.
    const float2 corners[3] = {
        float2(-1.0, 1.0),
        float2(3.0, 1.0),
        float2(-1.0, -3.0),
    };
    VSOutput output;
    output.pos = float4(corners[id], params.x, 1.0);
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET {
    return tint;
}
