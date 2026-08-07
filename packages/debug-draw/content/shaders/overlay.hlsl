cbuffer ScreenConstants : register(b0) {
    float2 screen_size;
};

struct VSInput {
    float3 pos : POSITION;
};

struct PSInput {
    float4 pos : SV_POSITION;
};

PSInput vs_main(VSInput input) {
    PSInput output;
    const float2 ndc = float2(
        input.pos.x / screen_size.x * 2.0 - 1.0,
        1.0 - input.pos.y / screen_size.y * 2.0);
    output.pos = float4(ndc, 0.0, 1.0);
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET {
    return float4(0.95, 0.95, 0.95, 0.92);
}
