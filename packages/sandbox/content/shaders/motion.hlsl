#include "common.hlsli"

cbuffer MotionConstants : register(b0) {
    float4x4 view_proj;
    float4x4 model;
    float4x4 prev_view_proj;
    float4x4 prev_model;
    float4 jitter;
};

struct VSInput {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD;
};

struct PSInput {
    float4 pos       : SV_POSITION;
    float4 clip_curr : TEXCOORD0;
    float4 clip_prev : TEXCOORD1;
};

PSInput vs_main(VSInput input) {
    PSInput output;
    float4 world = mul(model, float4(input.pos, 1.0));
    output.clip_curr = mul(view_proj, world);
    output.clip_prev = mul(prev_view_proj, mul(prev_model, float4(input.pos, 1.0)));
    output.pos = output.clip_curr;
    output.pos.xy += jitter.xy * output.pos.w;
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET {
    float2 curr_uv = ndc_to_uv(input.clip_curr.xy / max(abs(input.clip_curr.w), 1e-8));
    float2 prev_uv = ndc_to_uv(input.clip_prev.xy / max(abs(input.clip_prev.w), 1e-8));
    return float4(curr_uv - prev_uv, 0.0, 1.0);
}
