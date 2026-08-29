#include "common.hlsli"

cbuffer BloomConstants : register(b0) {
    float4 texel_size;
    float4 threshold;
    float4 params;
};

Texture2D src : register(t0);
SamplerState linear_sampler : register(s0);

static const float kClamp = 20.0;

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

float luma(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float karis_weight(float3 c) {
    return 1.0 / (1.0 + luma(c));
}

float3 karis_box(float3 a, float3 b, float3 c, float3 d) {
    float wa = karis_weight(a);
    float wb = karis_weight(b);
    float wc = karis_weight(c);
    float wd = karis_weight(d);
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd + 1e-5);
}

float3 apply_knee(float3 color) {
    float br = max(color.r, max(color.g, color.b));
    float rq = clamp(br - threshold.y, 0.0, threshold.z);
    rq = rq * rq * threshold.w;
    float s = max(rq, br - threshold.x) / max(br, 1e-5);
    color *= s;
    float m = max(color.r, max(color.g, color.b));
    if (m > kClamp) {
        color *= kClamp / m;
    }
    return color;
}

float3 sample_src(float2 uv, bool prefilter) {
    float3 color = src.Sample(linear_sampler, uv).rgb;
    return prefilter ? apply_knee(color) : color;
}

float4 ps_main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;
    float2 texel = texel_size.xy;
    bool prefilter = params.x > 0.5;

    float3 a = sample_src(uv + float2(-2.0, -2.0) * texel, prefilter);
    float3 b = sample_src(uv + float2( 0.0, -2.0) * texel, prefilter);
    float3 c = sample_src(uv + float2( 2.0, -2.0) * texel, prefilter);
    float3 d = sample_src(uv + float2(-2.0,  0.0) * texel, prefilter);
    float3 e = sample_src(uv, prefilter);
    float3 f = sample_src(uv + float2( 2.0,  0.0) * texel, prefilter);
    float3 g = sample_src(uv + float2(-2.0,  2.0) * texel, prefilter);
    float3 h = sample_src(uv + float2( 0.0,  2.0) * texel, prefilter);
    float3 i = sample_src(uv + float2( 2.0,  2.0) * texel, prefilter);
    float3 j = sample_src(uv + float2(-1.0, -1.0) * texel, prefilter);
    float3 k = sample_src(uv + float2( 1.0, -1.0) * texel, prefilter);
    float3 l = sample_src(uv + float2(-1.0,  1.0) * texel, prefilter);
    float3 m = sample_src(uv + float2( 1.0,  1.0) * texel, prefilter);

    if (prefilter) {
        float3 center = karis_box(j, k, l, m);
        float3 g1 = karis_box(a, b, d, e);
        float3 g2 = karis_box(b, c, e, f);
        float3 g3 = karis_box(d, e, g, h);
        float3 g4 = karis_box(e, f, h, i);
        return float4(center * 0.5 + (g1 + g2 + g3 + g4) * 0.125, 1.0);
    }

    float3 o = (j + k + l + m) * 0.125;
    o += (a + b + d + e) * 0.03125;
    o += (b + c + e + f) * 0.03125;
    o += (d + e + g + h) * 0.03125;
    o += (e + f + h + i) * 0.03125;
    return float4(o, 1.0);
}
