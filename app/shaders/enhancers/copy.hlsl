// File : copy.hlsl
// Type: Compute Shader

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

    OutputTexture[DTid.xy] = InputTexture.Load(int3(DTid.xy, 0));
}
