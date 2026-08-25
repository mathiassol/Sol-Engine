cbuffer SkyConstants : register(b0) {
    float4x4 inv_view;
    float4 ndc_scale;
    float4 sun_direction;
    float4 sun_color;
};

TextureCube radiance_map : register(t0);
SamplerState radiance_sampler : register(s0);

static const float kIntensity = 1.0;
static const bool kSunDisk = true;
static const float kSunCorePower = 256.0;
static const float kSunGlowPower = 24.0;
static const float kSunCore = 6.0;
static const float kSunGlow = 0.35;

struct PSInput {
    float4 pos : SV_POSITION;
    float2 ndc : TEXCOORD;
};

PSInput vs_main(uint id : SV_VertexID) {
    PSInput output;
    float2 uv = float2((id << 1) & 2, id & 2);
    float2 clip = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    output.pos = float4(clip, 1.0, 1.0);
    output.ndc = clip;
    return output;
}

float3 apply_sun_disk(float3 dir, float3 sky) {
    if (!kSunDisk) {
        return sky;
    }
    float mu = saturate(dot(normalize(dir), normalize(sun_direction.xyz)));
    float core = pow(mu, kSunCorePower);
    float glow = pow(mu, kSunGlowPower);
    return sky + sun_color.rgb * (core * kSunCore + glow * kSunGlow);
}

float4 ps_main(PSInput input) : SV_TARGET {
    float2 ndc = input.ndc - ndc_scale.zw;
    float3 view_dir = float3(ndc.x * ndc_scale.x, ndc.y * ndc_scale.y, -1.0);
    float3 world_dir = normalize(mul((float3x3)inv_view, view_dir));
    float3 hdr = radiance_map.Sample(radiance_sampler, world_dir).rgb;
    hdr = apply_sun_disk(world_dir, hdr);
    return float4(hdr * kIntensity, 1.0);
}

