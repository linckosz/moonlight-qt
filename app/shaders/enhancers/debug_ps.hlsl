// File : debug_ps.hlsl

// Structure for the pixel shader input coming from the vertex shader

struct PS_INPUT
{
    float4 Pos : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

// Texture 2D and sampler
Texture2D tex : register(t0);
SamplerState samplerState : register(s0);

// Pixel Shader : Invert Texture colors
float4 main(PS_INPUT input) : SV_TARGET
{
    float4 color = tex.Sample(samplerState, input.TexCoord);
    float3 invertedColor = 1.0f - color.rgb;
    return float4(invertedColor, color.a);
}
