#include "common.hlsli"

Texture2D scene_color : register(t0);
Texture2D bloom_color : register(t1);
SamplerState linear_sampler : register(s0);

static const float kBloomIntensity = 0.06;

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
    float3 hdr = scene_color.Sample(linear_sampler, input.uv).rgb;
    hdr += bloom_color.Sample(linear_sampler, input.uv).rgb * kBloomIntensity;
    return float4(aces_fitted(hdr), 1.0);
}
