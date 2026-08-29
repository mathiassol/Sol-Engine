#include "common.hlsli"

cbuffer AAConstants : register(b0) {
    float4 texel_size;
};

Texture2D edges : register(t0);
SamplerState point_clamp : register(s0);

static const int kMaxSearch = 8;

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

float searchX(float2 uv, float dir) {
    float2 p = uv;
    [loop]
    for (int i = 0; i < kMaxSearch; ++i) {
        p.x += dir * texel_size.x;
        float e = edges.SampleLevel(point_clamp, p, 0).g;
        if (e < 0.5) {
            return float(i);
        }
    }
    return float(kMaxSearch);
}

float searchY(float2 uv, float dir) {
    float2 p = uv;
    [loop]
    for (int i = 0; i < kMaxSearch; ++i) {
        p.y += dir * texel_size.y;
        float e = edges.SampleLevel(point_clamp, p, 0).r;
        if (e < 0.5) {
            return float(i);
        }
    }
    return float(kMaxSearch);
}

float area(float d1, float d2, float e1, float e2) {
    float span = saturate(1.0 - (d1 + d2) / (2.0 * float(kMaxSearch)));
    float cross = saturate(max(e1, e2));
    return span * lerp(0.35, 1.0, cross);
}

float4 ps_main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;
    float2 e = edges.SampleLevel(point_clamp, uv, 0).rg;
    float4 weights = float4(0.0, 0.0, 0.0, 0.0);

    if (e.g > 0.5) {
        float d1 = searchX(uv, -1.0);
        float d2 = searchX(uv, 1.0);
        float2 left = uv + float2(-(d1 + 1.0) * texel_size.x, 0.0);
        float2 right = uv + float2((d2 + 1.0) * texel_size.x, 0.0);
        float c1 = edges.SampleLevel(point_clamp, left, 0).r;
        float c2 = edges.SampleLevel(point_clamp, right, 0).r;
        weights.r = area(d1, d2, c1, c2);
    }
    if (e.r > 0.5) {
        float d1 = searchY(uv, -1.0);
        float d2 = searchY(uv, 1.0);
        float2 up = uv + float2(0.0, -(d1 + 1.0) * texel_size.y);
        float2 down = uv + float2(0.0, (d2 + 1.0) * texel_size.y);
        float c1 = edges.SampleLevel(point_clamp, up, 0).g;
        float c2 = edges.SampleLevel(point_clamp, down, 0).g;
        weights.b = area(d1, d2, c1, c2);
    }
    return weights;
}
