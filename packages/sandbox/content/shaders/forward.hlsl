cbuffer FrameConstants : register(b0) {
    float4x4 view_proj;
    float4x4 model;
    float4x4 sun_view_proj;
    float4 sun_direction;
    float4 sun_color;
    float4 ambient;
    float4 camera_pos;
    float4 point_pos_radius[4];
    float4 point_color_intensity[4];
    float4 material_params;
};

Texture2D albedo_map : register(t0);
Texture2D shadow_map : register(t1);
TextureCube irradiance_map : register(t2);
TextureCube prefilter_map : register(t3);
Texture2D brdf_lut : register(t4);
Texture2D metallic_roughness_map : register(t5);
Texture2D normal_map : register(t6);
SamplerState albedo_sampler : register(s0);
SamplerComparisonState shadow_sampler : register(s1);
SamplerState ibl_sampler : register(s2);

static const float kPi = 3.14159265;
static const float kMinPerceptualRoughness = 0.045;
static const float kDielectricF0 = 0.04;
static const int kPcfTaps = 16;
static const float kPcfRadiusTexels = 3.0;
static const float kShadowBias = 0.0015;
static const float kGoldenAngle = 2.39996323;
static const float kIblIntensity = 1.0;
static const float kIblMaxLod = 4.0;

struct VSInput {
    float3 pos    : POSITION;
    float3 normal : NORMAL;
    float2 uv     : TEXCOORD;
};

struct PSInput {
    float4 pos       : SV_POSITION;
    float3 normal    : NORMAL;
    float2 uv        : TEXCOORD;
    float3 world_pos : TEXCOORD1;
};

PSInput vs_main(VSInput input) {
    PSInput output;
    float4 world = mul(model, float4(input.pos, 1.0));
    output.pos = mul(view_proj, world);
    output.world_pos = world.xyz;
    output.normal = mul((float3x3)model, input.normal);
    output.uv = float2(input.uv.x, 1.0 - input.uv.y);
    return output;
}

float interleaved_gradient_noise(float2 pixel) {
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

float2 vogel_disk(int i, int n, float phi) {
    float r = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * kGoldenAngle + phi;
    float s, c;
    sincos(theta, s, c);
    return float2(c, s) * r;
}

float sample_sun_shadow(float3 world_pos, float2 pixel_pos) {
    float4 shadow_h = mul(sun_view_proj, float4(world_pos, 1.0));
    if (shadow_h.w <= 0.0) {
        return 1.0;
    }
    float3 ndc = shadow_h.xyz / shadow_h.w;
    float2 uv = float2(ndc.x * 0.5 + 0.5, ndc.y * -0.5 + 0.5);
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) {
        return 1.0;
    }

    uint map_w, map_h;
    shadow_map.GetDimensions(map_w, map_h);
    float2 texel = 1.0 / float2(map_w, map_h);
    float phi = interleaved_gradient_noise(pixel_pos) * (kPi * 2.0);
    float depth = ndc.z - kShadowBias;

    float sum = 0.0;
    [unroll]
    for (int i = 0; i < kPcfTaps; ++i) {
        float2 offset = vogel_disk(i, kPcfTaps, phi) * kPcfRadiusTexels * texel;
        sum += shadow_map.SampleCmpLevelZero(shadow_sampler, uv + offset, depth);
    }
    return sum / float(kPcfTaps);
}

float D_GGX(float NoH, float alpha) {
    float a2 = alpha * alpha;
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (kPi * d * d);
}

float V_SmithGGXCorrelated(float NoV, float NoL, float alpha) {
    float a2 = alpha * alpha;
    float ggxl = NoV * sqrt((-NoL * a2 + NoL) * NoL + a2);
    float ggxv = NoL * sqrt((-NoV * a2 + NoV) * NoV + a2);
    return 0.5 / (ggxv + ggxl);
}

float3 F_Schlick(float LoH, float3 f0) {
    float f = pow(1.0 - saturate(LoH), 5.0);
    return f0 + (1.0 - f0) * f;
}

float3 evaluate_punctual(float3 n, float3 v, float3 l, float3 albedo, float metallic,
    float perceptual_roughness, float3 radiance) {
    float NoL = saturate(dot(n, l));
    if (NoL <= 0.0) {
        return float3(0.0, 0.0, 0.0);
    }
    float alpha = perceptual_roughness * perceptual_roughness;
    float3 h = normalize(v + l);
    float NoV = saturate(dot(n, v)) + 1e-5;
    float NoH = saturate(dot(n, h));
    float LoH = saturate(dot(l, h));

    float3 f0 = lerp(float3(kDielectricF0, kDielectricF0, kDielectricF0), albedo, metallic);
    float3 F = F_Schlick(LoH, f0);
    float D = D_GGX(NoH, alpha);
    float Vis = V_SmithGGXCorrelated(NoV, NoL, alpha);
    float3 Fr = (D * Vis) * F;

    float3 diffuse_color = albedo * (1.0 - metallic);
    float3 Fd = diffuse_color * (1.0 - F) * (1.0 / kPi);
    return (Fd + Fr) * radiance * NoL;
}

float3 evaluate_ibl(float3 n, float3 v, float3 albedo, float metallic, float perceptual_roughness) {
    float3 f0 = lerp(float3(kDielectricF0, kDielectricF0, kDielectricF0), albedo, metallic);
    float NoV = saturate(dot(n, v)) + 1e-5;
    float3 r = reflect(-v, n);
    float lod = perceptual_roughness * kIblMaxLod;
    float3 irradiance = irradiance_map.Sample(ibl_sampler, n).rgb;
    float3 prefiltered = prefilter_map.SampleLevel(ibl_sampler, r, lod).rgb;
    float2 dfg = brdf_lut.Sample(ibl_sampler, float2(NoV, perceptual_roughness)).rg;
    float3 fd = albedo * (1.0 - metallic) * irradiance;
    float3 fr = prefiltered * (f0 * dfg.x + dfg.y);
    return (fd + fr) * kIblIntensity;
}

float3 sample_normal(float3 n, float3 world_pos, float2 uv) {
    float3 map = normal_map.Sample(albedo_sampler, uv).xyz * 2.0 - 1.0;
    float3 dp1 = ddx(world_pos);
    float3 dp2 = ddy(world_pos);
    float2 duv1 = ddx(uv);
    float2 duv2 = ddy(uv);
    float3 dp2perp = cross(dp2, n);
    float3 dp1perp = cross(n, dp1);
    float3 t = dp2perp * duv1.x + dp1perp * duv2.x;
    float3 b = dp2perp * duv1.y + dp1perp * duv2.y;
    float invmax = rsqrt(max(dot(t, t), dot(b, b)));
    t *= invmax;
    b *= invmax;
    return normalize(t * map.x + b * map.y + n * map.z);
}

float4 ps_main(PSInput input) : SV_TARGET {
    float3 n = sample_normal(normalize(input.normal), input.world_pos, input.uv);
    float3 albedo = albedo_map.Sample(albedo_sampler, input.uv).rgb;
    float4 mr = metallic_roughness_map.Sample(albedo_sampler, input.uv);
    float metallic = saturate(material_params.x * mr.b);
    float roughness = clamp(material_params.y * mr.g, kMinPerceptualRoughness, 1.0);
    float3 view_dir = normalize(camera_pos.xyz - input.world_pos);
    float3 lit = evaluate_ibl(n, view_dir, albedo, metallic, roughness);

    float3 sun_l = normalize(sun_direction.xyz);
    float shadow = saturate(dot(n, sun_l)) > 0.0
        ? sample_sun_shadow(input.world_pos, input.pos.xy)
        : 1.0;
    lit += evaluate_punctual(n, view_dir, sun_l, albedo, metallic, roughness,
        sun_color.rgb * shadow);

    [unroll]
    for (int i = 0; i < 4; ++i) {
        float radius = point_pos_radius[i].w;
        float intensity = point_color_intensity[i].w;
        if (radius > 0.0 && intensity > 0.0) {
            float3 to_light = point_pos_radius[i].xyz - input.world_pos;
            float dist = length(to_light);
            float atten = saturate(1.0 - dist / radius);
            atten *= atten;
            float3 l = to_light / max(dist, 1e-4);
            float3 radiance = point_color_intensity[i].rgb * intensity * atten;
            lit += evaluate_punctual(n, view_dir, l, albedo, metallic, roughness, radiance);
        }
    }

    return float4(lit, 1.0);
}
