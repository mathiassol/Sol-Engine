#include "instancing.hlsli"

cbuffer ShadowConstants : register(b0) {
    float4x4 view_proj;
    uint4 instance_base;
};

float4 vs_main(float3 pos : POSITION, uint id : SV_InstanceID) : SV_POSITION {
    InstanceData inst = sol_instance(id, instance_base);
    return mul(view_proj, mul(inst.model, float4(pos, 1.0)));
}
