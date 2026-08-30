#include "common.hlsli"

// Colour-space gate: proves the HLSL sRGB curve matches the C++ one.
//
// The curve necessarily exists twice — engine::math in C++, common.hlsli in
// HLSL — because the CPU builds mip chains and the GPU encodes the final
// image. Nothing but this readback stops the two drifting apart.
//
// Four probes, each written as the raw float bit pattern so the CPU compares
// the exact value rather than a quantised byte:
//
//   0: srgb_encode(0.2140411)  -> 0.5        the midpoint anchor
//   1: srgb_encode(0.001)      -> 0.01292    the linear toe; pow() gives 0.0195
//   2: srgb_decode(0.5)        -> 0.2140411  the inverse
//   3: srgb_decode(srgb_encode(0.7)) -> 0.7  round trip
RWBuffer<uint> OutBuf : register(u0);

[numthreads(1, 1, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID) {
    OutBuf[0] = asuint(srgb_encode(0.2140411));
    OutBuf[1] = asuint(srgb_encode(0.001));
    OutBuf[2] = asuint(srgb_decode(0.5));
    OutBuf[3] = asuint(srgb_decode(srgb_encode(0.7)));
}
