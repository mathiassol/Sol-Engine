#include "common.hlsli"

Texture2D hdr_color : register(t0);
SamplerState linear_sampler : register(s0);

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
};

PSInput vs_main(uint id : SV_VertexID) {
    PSInput output;
    output.uv = fullscreen_uv(id);
    output.pos = fullscreen_position(output.uv);
    return output;
}

float3 aces_fitted(float3 x) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 ps_main(PSInput input) : SV_TARGET {
    float3 hdr = hdr_color.Sample(linear_sampler, input.uv).rgb;
    // Same as tonemap.hlsl: the fit returns linear, so the display encode is
    // applied here. This is the TAA path's tonemap; both must agree or toggling
    // AA would change image brightness.
    return float4(srgb_encode(aces_fitted(hdr)), 1.0);
}
