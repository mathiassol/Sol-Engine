#include "common.hlsli"

// .x exposure (linear multiplier), .y bloom intensity.
// Keep in sync with engine::renderer::tonemap::Constants.
cbuffer TonemapConstants : register(b0) {
    float4 params;
};

Texture2D scene_color : register(t0);
Texture2D bloom_color : register(t1);
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
    // Exposure scales the scene only. Bloom arrives already exposed - its first
    // downsample read scene_color and applied the same multiplier - so scaling
    // it again here would square the exposure in the glow.
    float3 hdr = scene_color.Sample(linear_sampler, input.uv).rgb * params.x;
    hdr += bloom_color.Sample(linear_sampler, input.uv).rgb * params.y;
    // Narkowicz's fit is linear-in, linear-out — he fitted it after removing the
    // 2.4 gamma from the Rec.709 ODT — so the display encode is ours to apply.
    // ldr_color and the swapchain are UNORM, so it happens here, once.
    return float4(srgb_encode(aces_fitted(hdr)), 1.0);
}
