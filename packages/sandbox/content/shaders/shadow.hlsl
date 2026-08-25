cbuffer ShadowConstants : register(b0) {
    float4x4 view_proj;
    float4x4 model;
};

float4 vs_main(float3 pos : POSITION) : SV_POSITION {
    return mul(view_proj, mul(model, float4(pos, 1.0)));
}
