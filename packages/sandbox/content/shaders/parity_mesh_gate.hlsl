// Vertex input parity: the same indexed mesh drawn through both backends.
//
// The RHI describes attributes by HLSL *semantic*; Vulkan wants a Location
// number. Measured from real SPIR-V, DXC assigns locations in the declaration
// order of this struct - POSITION 0, NORMAL 1, TEXCOORD 2 - and the engine
// already fills GraphicsPipelineDesc::attributes in that same order, so
// location is the array index. That equivalence is load-bearing, and this
// shader is what makes a break in it visible: every attribute reaches the
// output, so a wrong location is a wrong colour rather than a missing triangle.
struct VSInput {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD;
};

struct VSOutput {
    float4 pos    : SV_POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD;
};

VSOutput vs_main(VSInput input) {
    VSOutput output;
    output.pos = float4(input.pos, 1.0);
    output.normal = input.normal;
    output.uv = input.uv;
    return output;
}

float4 ps_main(VSOutput input) : SV_TARGET {
    // Quadrants rather than the interpolated value, so the readback compares
    // exact bytes: 0.2, 0.6 and 0.8 are 51, 153 and 204 in UNORM8 with no
    // rounding tie, and the probe texels sit far from the 0.5 boundary.
    //
    // Each channel answers a different question. R and G say the uv arrived and
    // which way round it is - swap the two locations and all four quadrants
    // collapse to one colour. B says the normal arrived at all, because uv
    // data in the normal slot would not be 1.0.
    return float4(input.uv.x > 0.5 ? 0.8 : 0.2,
                  input.uv.y > 0.5 ? 0.6 : 0.2,
                  input.normal.z,
                  1.0);
}
