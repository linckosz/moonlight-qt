#pragma once

#include "streaming/video/videoenhancement.h"

#include <d3d11_4.h>
#include "path.h"
#include <wrl/client.h>
#include <d3dcompiler.h>

// FSR1 Declaration
#define A_CPU
#include "ffx_a.h"
#include "ffx_fsr1.h"

using Microsoft::WRL::ComPtr;

#include <DirectXMath.h>
using namespace DirectX;

class D3D11VAShaders
{
public:
    enum class Enhancer {
        NONE,
        FSR1, // https://github.com/cdozdil/OptiScaler/tree/master/OptiScaler/shaders/fsr1
        NIS, // https://github.com/NVIDIAGameWorks/NVIDIAImageScaling/blob/main/NIS/NIS_Main.hlsl
        NIS_HALF, // https://github.com/NVIDIAGameWorks/NVIDIAImageScaling/blob/main/NIS/NIS_Main.hlsl
        NIS_SHARPEN, // https://github.com/NVIDIAGameWorks/NVIDIAImageScaling/blob/main/NIS/NIS_Main.hlsl
        NIS_SHARPEN_HALF, // https://github.com/NVIDIAGameWorks/NVIDIAImageScaling/blob/main/NIS/NIS_Main.hlsl
        RCAS, // https://github.com/cdozdil/OptiScaler/blob/master/OptiScaler/shaders/rcas/precompile/rcas.hlsl
        CAS, // https://gist.github.com/butterw/ceb89a68bc0aa3b0e317660fb4bacaa3
        UPSCALER, // https://github.com/cdozdil/OptiScaler/blob/master/OptiScaler/shaders/output_scaling/precompile/bcds_catmull.hlsl
        COPY, // Copy the input texture into output texture
        TESTCS, // Use Compute Shader (SRV->UAV) to invert the color to check if Shader are working
        TESTPS // Use Pixel Shader (SRV->RTV) to invert the color to check if Shader are working
    };
    // Note: There is also https://gist.github.com/atyuwen/78d6e810e6d0f7fd4aa6207d416f2eeb
    // I could not make it work but looks interesting, it is performance optimized for mobile (iPhone oriented),
    // is using Pixel shader, Upscaling and Sharpening in 1-Pass, and is half-precision (16-bit)

    bool static isUpscaler(Enhancer enhancer);
    bool static isSharpener(Enhancer enhancer);
    bool static isUsingShader(Enhancer enhancer);

    D3D11VAShaders(
        ID3D11Device* device,
        ID3D11DeviceContext* deviceContext,
        VideoEnhancement* videoEnhancement,
        ID3D11Texture2D* textureIn,
        ID3D11Resource* textureOut,
        int outWidth,
        int outHeight,
        int offsetTop,
        int offsetLeft,
        Enhancer enhancer,
        bool isHDR
        );
    ~D3D11VAShaders();

    void draw();

private:
    ComPtr<ID3D11Device> m_Device;
    ComPtr<ID3D11DeviceContext> m_DeviceContext;
    ComPtr<ID3D11Resource> m_TextureOut;
    int m_InWidth;
    int m_InHeight;
    int m_OutWidth;
    int m_OutHeight;
    int m_OffsetTop;
    int m_OffsetLeft;
    Enhancer m_Enhancer;
    bool m_IsHDR = false;
    D3D11_BOX m_DestBox;
    HRESULT m_Hr;
    ComPtr<ID3D11SamplerState> m_Sampler;
    std::array<ComPtr<ID3D11Buffer>, 3> m_PSConstantBuffers;
    std::array<ComPtr<ID3D11Buffer>, 3> m_CSConstantBuffers;
    std::array<ComPtr<ID3D11PixelShader>, 3> m_PixelShaders;
    std::array<ComPtr<ID3D11ComputeShader>, 3> m_ComputeShaders;
    std::array<ComPtr<ID3D11Texture2D>, 3> m_TextureSRVs;
    std::array<ComPtr<ID3D11Texture2D>, 3> m_TextureRTVs;
    std::array<ComPtr<ID3D11Texture2D>, 3> m_TextureUAVs;
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> m_SRVs;
    std::array<ComPtr<ID3D11RenderTargetView>, 3> m_RTVs;
    std::array<ComPtr<ID3D11UnorderedAccessView>, 3> m_UAVs;
    VideoEnhancement* m_VideoEnhancement;
    ComPtr<ID3DBlob> m_ShaderBlob;
    ComPtr<ID3DBlob> m_ErrorBlob;
    uint m_CompileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
    bool m_IsUpscaling = true;
    bool m_IsUsingShader = true;

    // NVIDIA Image Scaling
    bool m_IsHalfprecision = false; // true: 16-bit (half precision) / false: 32-bit (full precision)
    uint32_t m_BlockWidth = 32;
    uint32_t m_BlockHeight = 32;
    uint32_t m_ThreadGroupSize = 128;

    bool createPixelShaderConstantBuffer(void* data, size_t size, uint slot);
    bool createComputeShaderConstantBuffer(void* data, size_t size, uint slot);
    bool createSampler();
    bool createSRV(ID3D11Resource* textureSRV, uint slot);
    bool createRTV(ID3D11Resource* textureRTV, uint slot);
    bool createUAV(ID3D11Resource* textureUAV, uint slot);
    bool createTexture(ComPtr<ID3D11Texture2D>& texture, int width, int height, bool isSRV, bool isRTV, bool isUAV);
    // Initialization
    bool initializeFSR1_EASU(uint slot = 0);
    bool initializeFSR1_RCAS(uint slot = 0);
    bool initializeNIS_UPSCALE_SHARPEN(bool isUpscaling = true);
    bool initializeRCAS(uint slot = 0);
    bool initializeCAS(uint slot = 0);
    bool initializeUPSCALER(uint slot = 0);
    bool initializeCOPY(uint slot = 0);
    // Rendering
    bool applyFSR1();
    bool applyNIS();
    bool applyRCAS();
    bool applyCAS();
    bool applyUPSCALER();
    bool applyCOPY();
    // Debug
    ComPtr<ID3D11Texture2D> m_TextureTestSource;
    ComPtr<ID3D11Texture2D> m_TextureTestDest;
    bool initializeTESTCS(uint slot = 0);
    bool initializeTESTPS(uint slot = 0);
    bool applyTESTCS();
    bool applyTESTPS();
    void setTextureTest(ID3D11Texture2D* texture);
    void setTextureTest(ID3D11Resource* texture);
    void copyTextureTest(QString imageName = "TextureTest.png");
    // NVIDIA Image Scaling
    void NIScreateSRV(ID3D11Resource* pResource, DXGI_FORMAT format, ID3D11ShaderResourceView** ppSRView);
    void NIScreateTexture2D(int w, int h, DXGI_FORMAT format, D3D11_USAGE heapType, const void* data, uint32_t rowPitch, uint32_t imageSize, ID3D11Texture2D** ppTexture2D);
    void NISgetTextureData(ID3D11Texture2D* texture, std::vector<uint8_t>& data, uint32_t& width, uint32_t& height, uint32_t& rowPitch);
    void NIScreateConstBuffer(void* initialData, uint32_t size, ID3D11Buffer** ppBuffer);
    void NISupdateConstBuffer(void* data, uint32_t size, ID3D11Buffer* ppBuffer);

    // We have 4 type of basic upscalers available, bcus is providing the best result (no text artifact)
    // bcds_catmull.hlsl, bcds_lanczos.hlsl, bcds_magc.hlsl, bcus.hlsl
    QString m_UpscalerHLSL = "bcus.hlsl";

    // FSR1
    struct alignas(16) FSR1EASUConstants {
        AU1 const0[4];
        AU1 const1[4];
        AU1 const2[4];
        AU1 const3[4]; // store output offset in final 2
        AU1 projCentre[2];
        AU1 squaredRadius;
        AU1 _padding;
    };

    struct alignas(16) FSR1RCASConstants {
        AU1 const0[4]; // store output offset in final 2
        AU1 projCentre[2];
        AU1 squaredRadius;
        AU1 debugMode;
    };

    // CAS
    struct alignas(16) CASConstant
    {
        float px; // 1.0f / width
        float py; // 1.0f / height
        DirectX::XMFLOAT2 wh; // width, height
        UINT counter;
        float clock;
    };

    // RCAS
    struct alignas(16) RCASConstant
    {
        float Sharpness;
        float Contrast;

        int DynamicSharpenEnabled;
        int DisplaySizeMV;
        int Debug;

        float MotionSharpness;
        float MotionTextureScale;
        float MvScaleX;
        float MvScaleY;

        float Threshold;
        float ScaleLimit;

        int DisplayWidth;
        int DisplayHeight;
    };

    // TEXTCS
    struct alignas(16) TEXTCSConstant
    {
        uint Width;
        uint Height;
        uint padding1;
        uint padding2;
    };

    // UPSCALER
    struct alignas(16) UPSCALERConstant
    {
        int _SrcWidth;
        int _SrcHeight;
        int _DstWidth;
        int _DstHeight;
    };

};

