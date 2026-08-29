#include "common.hlsli"

cbuffer BloomConstants : register(b0) {
    float4 texel_size;
    float4 threshold;
    float4 params;
};

Texture2D src_low : register(t0);
Texture2D src_high : register(t1);
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

float4 ps_main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;
    float2 texel = texel_size.xy;
    float3 o = src_low.Sample(linear_sampler, uv + float2(-1.0, -1.0) * texel).rgb;
    o += src_low.Sample(linear_sampler, uv + float2( 0.0, -1.0) * texel).rgb * 2.0;
    o += src_low.Sample(linear_sampler, uv + float2( 1.0, -1.0) * texel).rgb;
    o += src_low.Sample(linear_sampler, uv + float2(-1.0,  0.0) * texel).rgb * 2.0;
    o += src_low.Sample(linear_sampler, uv).rgb * 4.0;
    o += src_low.Sample(linear_sampler, uv + float2( 1.0,  0.0) * texel).rgb * 2.0;
    o += src_low.Sample(linear_sampler, uv + float2(-1.0,  1.0) * texel).rgb;
    o += src_low.Sample(linear_sampler, uv + float2( 0.0,  1.0) * texel).rgb * 2.0;
    o += src_low.Sample(linear_sampler, uv + float2( 1.0,  1.0) * texel).rgb;
    o = o * (1.0 / 16.0) + src_high.Sample(linear_sampler, uv).rgb;
    return float4(o, 1.0);
}
