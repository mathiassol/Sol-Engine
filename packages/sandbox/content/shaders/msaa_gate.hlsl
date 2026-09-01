// Two halves of the MSAA gate.
//
// The draw is a triangle whose hypotenuse crosses the target diagonally. A
// diagonal is the case multisampling exists for: on a 1x target every texel is
// wholly inside or wholly outside, so the edge is a staircase of pure white and
// pure black. On a 4x target resolved down, the texels the edge crosses come out
// as a coverage-weighted blend, and *that* is what the gate counts.

struct VSOutput {
    float4 pos : SV_POSITION;
};

VSOutput vs_main(uint id : SV_VertexID) {
    // Clip-space directly, so the gate needs no vertex or constant buffer. The
    // hypotenuse runs corner to corner at 45 degrees.
    float2 corners[3] = {
        float2(-1.0,  1.0),
        float2( 1.0,  1.0),
        float2(-1.0, -1.0),
    };
    VSOutput output;
    output.pos = float4(corners[id], 0.0, 1.0);
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET {
    return float4(1.0, 1.0, 1.0, 1.0);
}

// ── The measurement ──────────────────────────────────────────────────────────
//
// Counts texels that are neither fully covered nor fully empty. A single-sample
// render can only produce 0 or 255, so its count is zero by construction; a
// resolved 4x render produces 25%, 50% and 75% steps along the diagonal. Slot 0
// receives that count, slot 1 the number of fully-lit texels, which proves the
// draw actually happened rather than the target being empty.

Texture2D<float4> Source : register(t0);
RWBuffer<uint> Counts : register(u0);

[numthreads(8, 8, 1)]
void cs_count(uint3 dtid : SV_DispatchThreadID) {
    uint width = 0;
    uint height = 0;
    Source.GetDimensions(width, height);
    if (dtid.x >= width || dtid.y >= height) {
        return;
    }
    const float red = Source.Load(int3(dtid.xy, 0)).r;
    // 1/255 either side, so an exactly-black or exactly-white texel is excluded
    // and any resolve blend is not.
    if (red > 0.004 && red < 0.996) {
        InterlockedAdd(Counts[0], 1);
    } else if (red >= 0.996) {
        InterlockedAdd(Counts[1], 1);
    }
}
