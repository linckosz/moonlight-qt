// Copy processed Y Plane back to NV12 texture
Texture2D<float> gYTexture : register(t0);      // Texture R32_FLOAT source (Y traité)
RWTexture2D<float> gYUVTexture : register(u0);  // Texture NV12 destination

[numthreads(16, 16, 1)]
void mainCS(uint3 DTid : SV_DispatchThreadID)
{   
    // Lire la valeur Y traitée
    float y = gYTexture.Load(int3(DTid.xy, 0));
    
    // Écrire dans le plan Y de la texture NV12
    // Le plan Y occupe les premières 'height' lignes de la texture NV12
    gYUVTexture[DTid.xy] = y;
}
