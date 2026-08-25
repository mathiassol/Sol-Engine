cbuffer AAConstants : register(b0) {
    float4 texel_size;
};

Texture2D src : register(t0);
Texture2D weights : register(t1);
SamplerState linear_clamp : register(s0);
SamplerState point_clamp : register(s1);

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

float4 ps_main(PSInput input) : SV_TARGET {
    float2 uv = input.uv;
    float2 texel = texel_size.xy;
    float4 color = src.Sample(linear_clamp, uv);

    float a_top = weights.SampleLevel(point_clamp, uv, 0).r;
    float a_left = weights.SampleLevel(point_clamp, uv, 0).b;
    float a_bottom = weights.SampleLevel(point_clamp, uv + float2(0.0, texel.y), 0).r;
    float a_right = weights.SampleLevel(point_clamp, uv + float2(texel.x, 0.0), 0).b;

    float h = max(a_top, a_bottom);
    float v = max(a_left, a_right);
    float s = max(h, v);
    if (s < 1e-4) {
        return color;
    }

    float2 offset = (h > v)
        ? float2(0.0, a_top > a_bottom ? -a_top : a_bottom)
        : float2(a_left > a_right ? -a_left : a_right, 0.0);
    float4 opposite = src.Sample(linear_clamp, uv + offset * texel);
    return lerp(color, opposite, s);
}
