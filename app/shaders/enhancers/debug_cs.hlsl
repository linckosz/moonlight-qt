// File : debug_cs.hlsl

// SRV
Texture2D<unorm float4> InputTexture : register(t0);

// UAV
RWTexture2D<unorm float4> OutputTexture : register(u0);

// Constants
cbuffer TextureSize : register(b0)
{
    uint Width;
    uint Height;
    uint padding1;
    uint padding2;
};

[numthreads(16, 16, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Width || DTid.y >= Height)
        return;

    float4 color = InputTexture.Load(int3(DTid.xy, 0));
    OutputTexture[DTid.xy] = float4(1.0 - color.rgb, color.a);
}
