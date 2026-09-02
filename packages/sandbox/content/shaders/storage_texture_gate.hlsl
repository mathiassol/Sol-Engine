// Proves a storage texture is a real, writable, shared resource - not that a
// dispatch returned.
//
// One 8x8 group writes a position-dependent pattern, syncs, then thread (0,0)
// reads four texels *other* threads wrote and packs them into a buffer the gate
// can read back. Writing and reading the same texel from one thread would prove
// nothing; reading another thread's write is the actual claim.
// [[vk::image_format]] is a SPIR-V-backend annotation the DXIL path ignores,
// so this stays one shader source. Without it DXC gives an unannotated
// RWTexture2D<float4> the Rgba32f format operand, and Vulkan says a storage
// image whose shader format disagrees with its view format produces undefined
// values for the *whole* image - not just the texel being written. The other
// backend takes the format from the view and never notices.
[[vk::image_format("rgba8")]]
RWTexture2D<float4> OutTex : register(u0);
RWBuffer<uint> OutBuf : register(u1);

[numthreads(8, 8, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID) {
    // Exactly representable in 8-bit UNORM, so the gate compares for equality
    // rather than against a tolerance.
    const float x = (float)dtid.x / 255.0;
    const float y = (float)dtid.y / 255.0;
    OutTex[dtid.xy] = float4(x, y, 0.5, 1.0);

    AllMemoryBarrierWithGroupSync();

    if (dtid.x == 0 && dtid.y == 0) {
        // Four texels, none of them this thread's own.
        const uint2 probes[4] = {uint2(1, 0), uint2(0, 1), uint2(7, 3), uint2(3, 7)};
        for (uint i = 0; i < 4; ++i) {
            const float4 texel = OutTex[probes[i]];
            OutBuf[i] = (uint)round(texel.x * 255.0) | ((uint)round(texel.y * 255.0) << 8);
        }
    }
}
