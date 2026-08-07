cbuffer FrameConstants : register(b0) {
    float4x4 view_proj;
};

struct VSInput {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
};

struct PSInput {
    float4 pos    : SV_POSITION;
    float3 normal : NORMAL;
};

PSInput vs_main(VSInput input) {
    PSInput output;
    output.pos = mul(view_proj, float4(input.pos, 1.0));
    output.normal = input.normal;
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET {
    float3 n = normalize(input.normal);
    return float4(n * 0.5 + 0.5, 1.0);
}
