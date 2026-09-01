#include "common.hlsli"

cbuffer TaaConstants : register(b0) {
    float4 texel_size;
    float4 params;
    float4 jitter; // xy = UV offset (sample current at uv - xy), zw = NDC
};

Texture2D scene_color : register(t0);
Texture2D bloom_color : register(t1);
Texture2D motion_vectors : register(t2);
Texture2D history_color : register(t3);
SamplerState linear_clamp : register(s0);
SamplerState point_clamp : register(s1);

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

float3 rgb_to_ycocg(float3 c) {
    return float3(
        0.25 * c.r + 0.5 * c.g + 0.25 * c.b,
        0.5 * c.r - 0.5 * c.b,
        -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}

float3 ycocg_to_rgb(float3 c) {
    return float3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p) {
    float3 center = 0.5 * (aabb_max + aabb_min);
    float3 extents = 0.5 * (aabb_max - aabb_min);
    extents = max(extents, 1e-5);
    float3 v = p - center;
    float3 r = abs(v / extents);
    float m = max(max(r.x, r.y), r.z);
    if (m > 1.0) {
        return center + v / m;
    }
    return p;
}

float3 catmull_rom_sample(Texture2D tex, SamplerState samp, float2 uv, float2 texel) {
    float2 pos = uv / texel - 0.5;
    float2 f = frac(pos);
    float2 base = (floor(pos) - 1.0) * texel;
    float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
    float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
    float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
    float2 w3 = f * f * (-0.5 + 0.5 * f);
    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
    float2 t0 = base + texel * (w1 / s0);
    float2 t1 = base + texel * (2.0 + w3 / s1);
    float3 c00 = tex.SampleLevel(samp, float2(t0.x, t0.y), 0).rgb;
    float3 c10 = tex.SampleLevel(samp, float2(t1.x, t0.y), 0).rgb;
    float3 c01 = tex.SampleLevel(samp, float2(t0.x, t1.y), 0).rgb;
    float3 c11 = tex.SampleLevel(samp, float2(t1.x, t1.y), 0).rgb;
    return (c00 * s0.x + c10 * s1.x) * s0.y + (c01 * s0.x + c11 * s1.x) * s1.y;
}

float4 ps_main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;
    float2 texel = texel_size.xy;
    float reset = params.x;
    float history_weight = params.y;
    float bloom_intensity = params.z;
    // Must match tonemap.hlsl exactly: scene scaled, bloom not (it arrives
    // exposed). History needs no scaling either - it was written post-exposure.
    // If these two paths disagree, F5 changes image brightness.
    float exposure = params.w;
    float2 current_uv = uv - jitter.xy;

    float3 neighborhood_min = 10000.0;
    float3 neighborhood_max = -10000.0;
    float3 current = 0.0;
    float current_weight = 0.0;

    [unroll]
    for (int y = -1; y <= 1; y++) {
        [unroll]
        for (int x = -1; x <= 1; x++) {
            float2 offset = float2(x, y);
            float2 sample_uv = current_uv + offset * texel;
            float3 neighbor
                = max(0.0, scene_color.SampleLevel(linear_clamp, sample_uv, 0).rgb) * exposure;
            neighbor += bloom_color.SampleLevel(linear_clamp, sample_uv, 0).rgb * bloom_intensity;
            float w = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
            current += neighbor * w;
            current_weight += w;
            float3 ycc = rgb_to_ycocg(neighbor);
            neighborhood_min = min(neighborhood_min, ycc);
            neighborhood_max = max(neighborhood_max, ycc);
        }
    }
    current /= max(current_weight, 1e-5);

    if (reset > 0.5) {
        return float4(current, 1.0);
    }

    float2 motion = motion_vectors.SampleLevel(point_clamp, uv, 0).xy;
    float2 history_uv = uv - motion;
    if (any(history_uv != saturate(history_uv))) {
        return float4(current, 1.0);
    }

    float3 history = max(0.0, catmull_rom_sample(history_color, linear_clamp, history_uv, texel));
    float3 clipped
        = ycocg_to_rgb(clip_aabb(neighborhood_min, neighborhood_max, rgb_to_ycocg(history)));
    float3 resolved = lerp(current, clipped, history_weight);
    return float4(resolved, 1.0);
}
