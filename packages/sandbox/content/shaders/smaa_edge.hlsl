cbuffer AAConstants : register(b0) {
    float4 texel_size;
};

Texture2D src : register(t0);
SamplerState linear_clamp : register(s0);

static const float kThreshold = 0.1;
static const float kLocalContrast = 2.0;

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
};

PSInput vs_main(uint id : SV_VertexID) {
    PSInput output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float luma(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 ps_main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;
    float2 texel = texel_size.xy;
    float L = luma(src.Sample(linear_clamp, uv).rgb);
    float Lleft = luma(src.Sample(linear_clamp, uv + float2(-1.0, 0.0) * texel).rgb);
    float Ltop = luma(src.Sample(linear_clamp, uv + float2(0.0, -1.0) * texel).rgb);
    float2 delta = abs(L - float2(Lleft, Ltop));
    float2 edges = step(kThreshold, delta);

    float Lright = luma(src.Sample(linear_clamp, uv + float2(1.0, 0.0) * texel).rgb);
    float Lbottom = luma(src.Sample(linear_clamp, uv + float2(0.0, 1.0) * texel).rgb);
    float2 delta2 = abs(L - float2(Lright, Lbottom));
    float2 maxDelta = max(delta, delta2);
    float maxNeighbor = max(maxDelta.x, maxDelta.y);
    edges *= step(maxNeighbor / kLocalContrast, delta);

    return float4(edges, 0.0, 1.0);
}
