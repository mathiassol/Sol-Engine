RWBuffer<uint> OutBuf : register(u0);

[numthreads(1, 1, 1)]
void cs_main(uint3 dtid : SV_DispatchThreadID) {
    OutBuf[0] = 0xC0DE0001u;
}
