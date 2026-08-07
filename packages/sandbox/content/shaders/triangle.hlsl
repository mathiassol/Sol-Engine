struct VSInput {
    float3 pos : POSITION;
    float3 col : COLOR;
};

struct PSInput {
    float4 pos : SV_POSITION;
    float3 col : COLOR;
};

PSInput vs_main(VSInput input) {
    PSInput output;
    output.pos = float4(input.pos, 1.0);
    output.col = input.col;
    return output;
}

float4 ps_main(PSInput input) : SV_TARGET {
    return float4(input.col, 1.0);
}
