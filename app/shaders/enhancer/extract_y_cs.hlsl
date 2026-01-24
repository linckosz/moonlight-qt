// Extract Y Plane and past it to a R32 Texture
Texture2D<float> gYUVTexture : register(t0);
RWTexture2D<float> gYTexture : register(u0);

[numthreads(16,16,1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{   
    float y = gYUVTexture.Load(int3(DTid.xy, 0)).r; 

    gYTexture[DTid.xy] = y;
}
