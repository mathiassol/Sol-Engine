cbuffer AAConstants : register(b0) {
    float4 texel_size;
};

Texture2D src : register(t0);
SamplerState linear_clamp : register(s0);

struct PSInput {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD;
};

PSInput vs_main(uint id : SV_VertexID) {
    PSInput output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.pos = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

float luma(float3 c) {
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

float4 ps_main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;
    float2 texel = texel_size.xy;

    float3 rgbN = src.Sample(linear_clamp, uv + float2(0.0, -1.0) * texel).rgb;
    float3 rgbW = src.Sample(linear_clamp, uv + float2(-1.0, 0.0) * texel).rgb;
    float3 rgbM = src.Sample(linear_clamp, uv).rgb;
    float3 rgbE = src.Sample(linear_clamp, uv + float2(1.0, 0.0) * texel).rgb;
    float3 rgbS = src.Sample(linear_clamp, uv + float2(0.0, 1.0) * texel).rgb;

    float lumaN = luma(rgbN);
    float lumaW = luma(rgbW);
    float lumaM = luma(rgbM);
    float lumaE = luma(rgbE);
    float lumaS = luma(rgbS);

    float lumaMin = min(lumaM, min(min(lumaN, lumaW), min(lumaS, lumaE)));
    float lumaMax = max(lumaM, max(max(lumaN, lumaW), max(lumaS, lumaE)));
    float range = lumaMax - lumaMin;
    if (range < max(0.0833, lumaMax * 0.166)) {
        return float4(rgbM, 1.0);
    }

    float3 rgbNW = src.Sample(linear_clamp, uv + float2(-1.0, -1.0) * texel).rgb;
    float3 rgbNE = src.Sample(linear_clamp, uv + float2(1.0, -1.0) * texel).rgb;
    float3 rgbSW = src.Sample(linear_clamp, uv + float2(-1.0, 1.0) * texel).rgb;
    float3 rgbSE = src.Sample(linear_clamp, uv + float2(1.0, 1.0) * texel).rgb;
    float lumaNW = luma(rgbNW);
    float lumaNE = luma(rgbNE);
    float lumaSW = luma(rgbSW);
    float lumaSE = luma(rgbSE);

    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y = ((lumaNW + lumaSW) - (lumaNE + lumaSE));
    float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125, 0.0078125);
    float rcpDir = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDir, float2(-8.0, -8.0), float2(8.0, 8.0)) * texel;

    float3 rgbA = 0.5 * (
        src.Sample(linear_clamp, uv + dir * (1.0 / 3.0 - 0.5)).rgb +
        src.Sample(linear_clamp, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (
        src.Sample(linear_clamp, uv + dir * -0.5).rgb +
        src.Sample(linear_clamp, uv + dir * 0.5).rgb);
    float lumaB = luma(rgbB);
    if (lumaB < lumaMin || lumaB > lumaMax) {
        return float4(rgbA, 1.0);
    }
    return float4(rgbB, 1.0);
}
