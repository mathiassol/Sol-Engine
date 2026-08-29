#ifndef SOL_INSTANCING_HLSLI
#define SOL_INSTANCING_HLSLI

// Per-instance data, read from a root SRV in register space 1.
//
// Space 1 keeps this clear of the t0.. texture registers. A *root* SRV, not a
// descriptor table, because the table path in this engine is pixel-visible
// only and this is consumed by the vertex shader.
//
// This struct mirrors engine::renderer::InstanceData byte for byte. A
// StructuredBuffer element packs like a C struct - unlike a cbuffer, where a
// float3 would occupy a full 16-byte register - so the two stay in step as
// long as both are built from float4x4 / float4. The C++ side static_asserts
// sizeof == 144.
struct InstanceData {
    float4x4 model;        // 64
    float4x4 prev_model;   // 64
    float4 material;       // 16  (x = metallic, y = roughness)
};

StructuredBuffer<InstanceData> sol_instances : register(t0, space1);

// SV_InstanceID restarts at 0 for every draw and does NOT include
// StartInstanceLocation, so the batch's base comes from the pass constant
// buffer. That is also the portable choice: Vulkan folds firstInstance into
// gl_InstanceIndex and Metal splits it out again, so relying on it would make
// this shader read different data per backend.
InstanceData sol_instance(uint instance_id, uint4 instance_base) {
    return sol_instances[instance_base.x + instance_id];
}

#endif // SOL_INSTANCING_HLSLI
