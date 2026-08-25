cbuffer LineConstants : register(b0) {
    float4x4 view_proj;
};

struct VSInput {
    float3 pos   : POSITION;
    float3 color : COLOR;
};

struct PSInput {
    float4 pos   : SV_POSITION;
    float3 color : COLOR;
};

PSInput vs_main(VSInput input) {
    PSInput output;
    output.pos = mul(view_proj, float4(input.pos, 1.0));
    output.color = input.color;
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET {
    return float4(input.color, 1.0);
}
