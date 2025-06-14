#include "d3d11va_shaders.h"

#include "SDL_log.h"
#include "qimage.h"
#include <d3dcompiler.h>

// NIS Declaration
#include "NIS_Config.h"

using Microsoft::WRL::ComPtr;

D3D11VAShaders::D3D11VAShaders(
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
    ):
    m_Device(device),
    m_DeviceContext(deviceContext),
    m_TextureOut(textureOut),
    m_OutWidth(outWidth),
    m_OutHeight(outHeight),
    m_OffsetTop(offsetTop),
    m_OffsetLeft(offsetLeft),
    m_Enhancer(enhancer),
    m_IsHDR(isHDR),
    m_VideoEnhancement(videoEnhancement)
{
    m_PSConstantBuffers = { nullptr, nullptr };
    m_CSConstantBuffers = { nullptr, nullptr };
    m_PixelShaders = { nullptr, nullptr };
    m_ComputeShaders = { nullptr, nullptr };

    m_TextureSRVs[0] = textureIn;

    D3D11_TEXTURE2D_DESC desc;
    m_TextureSRVs[0]->GetDesc(&desc);
    m_InWidth = desc.Width;
    m_InHeight = desc.Height;

    createSampler();

    m_DestBox.left = 0;
    m_DestBox.top = 0;
    m_DestBox.right = m_OutWidth;
    m_DestBox.bottom = m_OutHeight;
    m_DestBox.front = 0;
    m_DestBox.back = 1;

    m_IsHalfprecision = false;
    m_IsUpscaling = isUpscaler(m_Enhancer);
    m_IsUsingShader = isUsingShader(m_Enhancer);

    switch (m_Enhancer) {
    case Enhancer::FSR1:
        initializeFSR1_EASU(0); // CS Upscaling
        initializeFSR1_RCAS(1); // CS Sharpening (I don't see any graphical difference, probably not setup properly)
        break;
    case Enhancer::NIS:
        initializeNIS_UPSCALE_SHARPEN(true); // CS Upscaling & Sharpening
        break;
    case Enhancer::NIS_HALF:
        // 16-bit is roughly 5% faster than 32-bit, with imperceptible visual impact
        m_IsHalfprecision = true;
        initializeNIS_UPSCALE_SHARPEN(true); // CS Upscaling & Sharpening
        break;
    case Enhancer::NIS_SHARPEN:
        initializeNIS_UPSCALE_SHARPEN(false); // CS Sharpening
        break;
    case Enhancer::NIS_SHARPEN_HALF:
        // 16-bit is roughly 5% faster than 32-bit, with imperceptible visual impact
        m_IsHalfprecision = true;
        initializeNIS_UPSCALE_SHARPEN(false); // CS Sharpening
        break;
    case Enhancer::RCAS:
        initializeRCAS(0); // CS Sharpening
        break;
    case Enhancer::CAS:
        initializeCAS(0); // PS Sharpening
        break;
    case Enhancer::UPSCALER:
        initializeUPSCALER(0); // CS Upscaling
        initializeRCAS(1); // CS Sharpening
        break;
    case Enhancer::COPY:
        initializeCOPY(0); // CS Copy
        break;
    case Enhancer::TESTCS:
        initializeTESTCS(0); // CS Inverting color
        break;
    case Enhancer::TESTPS:
        initializeTESTPS(0); // PS Inverting color
        break;
    default:
        break;
    }
}

D3D11VAShaders::~D3D11VAShaders()
{
    for (auto& constant : m_PSConstantBuffers) {
        constant.Reset();
    }

    for (auto& constant : m_CSConstantBuffers) {
        constant.Reset();
    }

    for (auto& shader : m_PixelShaders) {
        shader.Reset();
    }

    for (auto& shader : m_ComputeShaders) {
        shader.Reset();
    }

    for (auto& rtv : m_RTVs) {
        rtv.Reset();
    }

    m_ShaderBlob = nullptr;
    m_ErrorBlob = nullptr;
}

/**
 * \brief Apply shader to the input texture
 *
 * Apply Pixel shader(s) and/or Compute shader(s) accroding to the selected algorythm
 *
 * \return void
 */
void D3D11VAShaders::draw()
{
    if(!m_Device){
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "No shader applied, D3D11VAShaders needs to be instanciated first.");
        return;
    }

    switch (m_Enhancer) {
    case Enhancer::FSR1:
        applyFSR1();
        break;
    case Enhancer::NIS:
    case Enhancer::NIS_HALF:
        applyNIS();
        break;
    case Enhancer::NIS_SHARPEN:
    case Enhancer::NIS_SHARPEN_HALF:
        applyNIS();
        break;
    case Enhancer::RCAS:
        applyRCAS();
        break;
    case Enhancer::CAS:
        applyCAS();
        break;
    case Enhancer::UPSCALER:
        applyUPSCALER();
        break;
    case Enhancer::COPY:
        applyCOPY();
        break;
    case Enhancer::TESTCS:
        applyTESTCS();
        break;
    case Enhancer::TESTPS:
        applyTESTPS();
        break;
    default:
        // Do nothing
        break;
    }

    return;
}

/**
 * \brief Inform if the selected enhancer uses shaders to apply upscaling
 *
 * Inform if the selected enhancer uses shaders to apply upscaling
 *
 * \param in enhancer, Enhancer type
 * \return bool, returns True if the enhancer has upscaling in its pipeline
 */
bool D3D11VAShaders::isUpscaler(D3D11VAShaders::Enhancer enhancer)
{
    switch (enhancer) {
    // List all operations that are doing upscaling
    case D3D11VAShaders::Enhancer::FSR1:
    case D3D11VAShaders::Enhancer::NIS:
    case D3D11VAShaders::Enhancer::NIS_HALF:
    case D3D11VAShaders::Enhancer::UPSCALER:
        return true;
        break;
    default:
        break;
    }
    return false;
}

/**
 * \brief Inform if the selected enhancer uses shaders to apply sharpening
 *
 * Inform if the selected enhancer uses shaders to apply sharpening
 *
 * \param in enhancer, Enhancer type
 * \return bool, returns True if the enhancer has upscaling in its pipeline
 */
bool D3D11VAShaders::isSharpener(D3D11VAShaders::Enhancer enhancer)
{
    switch (enhancer) {
    // List all operations that are doing sharpening
    case D3D11VAShaders::Enhancer::FSR1:
    case D3D11VAShaders::Enhancer::NIS:
    case D3D11VAShaders::Enhancer::NIS_HALF:
    case D3D11VAShaders::Enhancer::NIS_SHARPEN:
    case D3D11VAShaders::Enhancer::NIS_SHARPEN_HALF:
    case D3D11VAShaders::Enhancer::RCAS:
    case D3D11VAShaders::Enhancer::CAS:
    case D3D11VAShaders::Enhancer::UPSCALER:
        return true;
        break;
    default:
        break;
    }
    return false;
}

/**
 * \brief Inform if a Shader operation is needed
 *
 * Inform if a Shader operation is needed
 *
 * \param in enhancer, Enhancer type
 * \return bool, returns True if the enhancer is using Shader
 */
bool D3D11VAShaders::isUsingShader(D3D11VAShaders::Enhancer enhancer)
{
    return enhancer != D3D11VAShaders::Enhancer::NONE;
}

/**
 * \brief Create a Pixel Shader Constant Buffer
 *
 * Create a Pixel Shader Constant Buffer
 *
 * \param in data, Constant buffer data
 * \param in size, Constant buffer size
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::createPixelShaderConstantBuffer(void* data, size_t size, uint slot)
{
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = static_cast<uint>(size);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    m_Hr = m_Device->CreateBuffer(&cbDesc, nullptr, m_PSConstantBuffers[slot].GetAddressOf());
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateBuffer() failed: %x",
                     m_Hr);
        return false;
    }

    if (!data || size == 0)
        return false;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    m_Hr = m_DeviceContext->Map(m_PSConstantBuffers[slot].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11DeviceContext::Unmap() failed: %x",
                     m_Hr);
        return false;
    }
    memcpy(mappedResource.pData, data, size);
    m_DeviceContext->Unmap(m_PSConstantBuffers[slot].Get(), 0);

    return true;
}

/**
 * \brief Create a Pixel Shader Constant Buffer
 *
 * Create a Compute Shader Constant Buffer
 *
 * \param in data, Constant buffer data
 * \param in size, Constant buffer size
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::createComputeShaderConstantBuffer(void* data, size_t size, uint slot)
{
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = static_cast<uint>(size);;
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    m_Hr = m_Device->CreateBuffer(&cbDesc, nullptr, m_CSConstantBuffers[slot].GetAddressOf());
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateBuffer() failed: %x",
                     m_Hr);
        return false;
    }

    if (!data || size == 0)
        return false;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    m_Hr = m_DeviceContext->Map(m_CSConstantBuffers[slot].Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11DeviceContext::Unmap() failed: %x",
                     m_Hr);
        return false;
    }
    memcpy(mappedResource.pData, data, size);
    m_DeviceContext->Unmap(m_CSConstantBuffers[slot].Get(), 0);

    return true;
}

/**
 * \brief Create a Sampler
 *
 * Create a Sampler
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::createSampler()
{
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samplerDesc.MinLOD = 0.0f;
    samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

    m_Hr = m_Device->CreateSamplerState(&samplerDesc,  m_Sampler.GetAddressOf());
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateSamplerState() failed: %x",
                     m_Hr);
        return false;
    }
    return true;
}

/**
 * \brief Create a Shader Resource View
 *
 * Create a Shader Resource View
 *
 * \param in textureSRV, Texture to use in the SRV
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::createSRV(ID3D11Resource* textureSRV, uint slot)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = m_IsHDR ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    HRESULT m_Hr = m_Device->CreateShaderResourceView(
        textureSRV,
        &srvDesc,
        m_SRVs[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateShaderResourceView() failed: %x",
                     m_Hr);
        return false;
    }
    return true;
}

/**
 * \brief Create a Render Target View
 *
 * Create a Render Target View
 *
 * \param in textureRTV, Texture to use in the RTV
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::createRTV(ID3D11Resource* textureRTV, uint slot)
{
    HRESULT m_Hr = m_Device->CreateRenderTargetView(
        textureRTV,
        nullptr,
        m_RTVs[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateRenderTargetView() failed: %x",
                     m_Hr);
        return false;
    }
    return true;
}

/**
 * \brief Create a Shader Resource View
 *
 * Create a Shader Resource View
 *
 * \param in textureUAV, Texture to use in the UAV
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::createUAV(ID3D11Resource* textureUAV, uint slot)
{
    HRESULT m_Hr = m_Device->CreateUnorderedAccessView(
        textureUAV,
        nullptr,
        m_UAVs[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateUnorderedAccessView() failed: %x",
                     m_Hr);
        return false;
    }
    return true;
}

/**
 * \brief Create a Texture
 *
 * Create a Texture
 *
 * \param in texture, Texture variable to instantiate
 * \param in slot, Index of the object array
 * \param in height, Texture height
 * \param in width, Texture width
 * \param in isSRV, at True it add BindFlags for SRV
 * \param in isRTV, at True it add BindFlags for RTV
 * \param in isUAV, at True it add BindFlags for UAV
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::createTexture(ComPtr<ID3D11Texture2D>& texture, int width, int height, bool isSRV, bool isRTV, bool isUAV)
{
    D3D11_TEXTURE2D_DESC textDesc = {};
    textDesc.Width = width;
    textDesc.Height = height;
    textDesc.MipLevels = 1;
    textDesc.ArraySize = 1;
    textDesc.Format = m_IsHDR ? DXGI_FORMAT_R10G10B10A2_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    textDesc.SampleDesc.Quality = 0;
    textDesc.SampleDesc.Count = 1;
    textDesc.Usage = D3D11_USAGE_DEFAULT;
    textDesc.CPUAccessFlags = 0;
    textDesc.BindFlags = 0;
    if(isSRV) textDesc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;
    if(isRTV) textDesc.BindFlags |= D3D11_BIND_RENDER_TARGET;
    if(isUAV) textDesc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS ;

    m_Hr = m_Device->CreateTexture2D(
        &textDesc,
        nullptr,
        texture.GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateTexture2D() failed: %x",
                     m_Hr);
        return false;
    }

    return true;
}

/**
 * \brief Initialize FSR1 EASU (pass1/2) operations
 *
 * Prepare Constant, Shader, SRV, UAV
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeFSR1_EASU(uint slot)
{
    // FSR1 Documentation implementation:
    // https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/docs/FidelityFX-FSR-Overview-Integration.pdf
    FSR1EASUConstants constantBuffer = {};
    FsrEasuCon(
        constantBuffer.const0,
        constantBuffer.const1,
        constantBuffer.const2,
        constantBuffer.const3,
        static_cast<AF1>(m_InWidth),
        static_cast<AF1>(m_InHeight),
        static_cast<AF1>(m_InWidth),
        static_cast<AF1>(m_InHeight),
        static_cast<AF1>(m_OutWidth),
        static_cast<AF1>(m_OutHeight)
        );

    createComputeShaderConstantBuffer(&constantBuffer, sizeof(constantBuffer), slot);

    QByteArray computeShaderBytecode = Path::readDataFile(":/enhancer/fsr1_easu.cso");
    m_Hr = m_Device->CreateComputeShader(
        computeShaderBytecode.constData(),
        computeShaderBytecode.length(),
        nullptr,
        m_ComputeShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateComputeShader() failed: %x",
                     m_Hr);
        return false;
    }

    if(!createSRV(m_TextureSRVs[slot].Get(), slot)){
        return false;
    }

    // Set the Texture as UAV (output of EASU, Pass1) and SRV (input of RCAS, Pass2)
    if(!createTexture(m_TextureUAVs[slot], m_OutWidth, m_OutHeight, true, false, true)){
        return false;
    }

    if(!createUAV(m_TextureUAVs[slot].Get(), slot)){
        return false;
    }

    return true;
}

/**
 * \brief Initialize FSR1 RCAS (pass2/2) operations
 *
 * Prepare Constant, Shader, SRV, UAV
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
// Note : I don't know why yet, but it seems not applying. It is better to use RCAS as pass2.
bool D3D11VAShaders::initializeFSR1_RCAS(uint slot)
{
    // FSR1 Documentation implementation:
    // https://github.com/GPUOpen-Effects/FidelityFX-FSR/blob/master/docs/FidelityFX-FSR-Overview-Integration.pdf
    FSR1RCASConstants constantBuffer = {};
    AF1 sharpeness = 0.2f;
    FsrRcasCon(
        constantBuffer.const0,
        sharpeness
        );

    createComputeShaderConstantBuffer(&constantBuffer, sizeof(constantBuffer), slot);

    QByteArray computeShaderBytecode = Path::readDataFile(":/enhancer/fsr1_rcas.cso");
    m_Hr = m_Device->CreateComputeShader(
        computeShaderBytecode.constData(),
        computeShaderBytecode.length(),
        nullptr,
        m_ComputeShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateComputeShader() failed: %x",
                     m_Hr);
        return false;
    }

    // RCAS is Pass2, we need to get UAV from Pass1 (EASU)
    if(!createSRV(m_TextureUAVs[slot-1].Get(), slot)){
        return false;
    }

    if(!createTexture(m_TextureUAVs[slot], m_OutWidth, m_OutHeight, false, false, true)){
        return false;
    }

    if(!createUAV(m_TextureUAVs[slot].Get(), slot)){
        return false;
    }

    return true;
}

/**
 * \brief Initialize NIS UPSCALE operations
 *
 * Prepare Constant, Shader, SRV, UAV
 *
 * \params in isUpscaling, True does Scaling then Sharpening, False does only Sharpening
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeNIS_UPSCALE_SHARPEN(bool isUpscaling)
{
    m_IsUpscaling = isUpscaling;

    struct NISConfig constantBuffer;
    NIScreateConstBuffer(&constantBuffer, sizeof(NISConfig), m_CSConstantBuffers[0].GetAddressOf());

    const int rowPitch = m_IsHalfprecision ? kFilterSize * sizeof(uint16_t) : kFilterSize * sizeof(float);
    const int coeffSize = rowPitch * kPhaseCount;

    if(m_IsHalfprecision){
        NIScreateTexture2D(
            kFilterSize / 4,
            kPhaseCount,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_USAGE_DEFAULT,
            coef_scale_fp16,
            rowPitch,
            coeffSize,
            m_TextureSRVs[1].GetAddressOf()
            );
        NIScreateTexture2D(
            kFilterSize / 4,
            kPhaseCount,
            DXGI_FORMAT_R16G16B16A16_FLOAT,
            D3D11_USAGE_DEFAULT,
            coef_usm_fp16,
            rowPitch,
            coeffSize,
            m_TextureSRVs[2].GetAddressOf()
            );
    } else {
        NIScreateTexture2D(
            kFilterSize / 4,
            kPhaseCount,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            D3D11_USAGE_DEFAULT,
            coef_scale,
            rowPitch,
            coeffSize,
            m_TextureSRVs[1].GetAddressOf()
            );
        NIScreateTexture2D(
            kFilterSize / 4,
            kPhaseCount,
            DXGI_FORMAT_R32G32B32A32_FLOAT,
            D3D11_USAGE_DEFAULT,
            coef_usm,
            rowPitch,
            coeffSize,
            m_TextureSRVs[2].GetAddressOf()
            );
    }

    createSRV(m_TextureSRVs[0].Get(), 0);
    if(m_IsHalfprecision){
        NIScreateSRV(m_TextureSRVs[1].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, m_SRVs[1].GetAddressOf());
        NIScreateSRV(m_TextureSRVs[2].Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, m_SRVs[2].GetAddressOf());
    } else {
        NIScreateSRV(m_TextureSRVs[1].Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, m_SRVs[1].GetAddressOf());
        NIScreateSRV(m_TextureSRVs[2].Get(), DXGI_FORMAT_R32G32B32A32_FLOAT, m_SRVs[2].GetAddressOf());
    }
    createTexture(m_TextureUAVs[0], m_OutWidth, m_OutHeight, false, false, true);
    createUAV(m_TextureUAVs[0].Get(), 0);

    float NISsharpness = 0.25f;
    if(m_IsUpscaling){
        if(m_IsHDR){
            NVScalerUpdateConfig(constantBuffer, NISsharpness,
                                 0, 0, m_InWidth, m_InHeight, m_InWidth, m_InHeight,
                                 0, 0, m_OutWidth, m_OutHeight, m_OutWidth, m_OutHeight,
                                 NISHDRMode::PQ);
        } else {
            NVScalerUpdateConfig(constantBuffer, NISsharpness,
                                 0, 0, m_InWidth, m_InHeight, m_InWidth, m_InHeight,
                                 0, 0, m_OutWidth, m_OutHeight, m_OutWidth, m_OutHeight,
                                 NISHDRMode::None);
        }
    } else {
        if(m_IsHDR){
            NVSharpenUpdateConfig(constantBuffer, NISsharpness,
                                 0, 0, m_InWidth, m_InHeight, m_InWidth, m_InHeight,
                                 0, 0, NISHDRMode::PQ);
        } else {
            NVSharpenUpdateConfig(constantBuffer, NISsharpness,
                                 0, 0, m_InWidth, m_InHeight, m_InWidth, m_InHeight,
                                 0, 0, NISHDRMode::None);
        }
    }

    NISupdateConstBuffer(&constantBuffer, sizeof(NISConfig), m_CSConstantBuffers[0].Get());

    QFile file(":/enhancer/NIS_Main.hlsl");
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not open the file");
        return false;
    }

    QByteArray hlslSource = file.readAll();
    file.close();

    // Default on NVIDIA_Generic
    NISGPUArchitecture GPUArchitecture = NISGPUArchitecture::NVIDIA_Generic;
    if(m_IsHalfprecision){
        GPUArchitecture = NISGPUArchitecture::NVIDIA_Generic_fp16;
    } else if(m_VideoEnhancement->isVendorAMD()){
        GPUArchitecture = NISGPUArchitecture::AMD_Generic;
    } else if(m_VideoEnhancement->isVendorIntel()){
        GPUArchitecture = NISGPUArchitecture::Intel_Generic;
    }
    NISOptimizer opt(m_IsUpscaling, GPUArchitecture);
    m_BlockWidth = opt.GetOptimalBlockWidth();
    m_BlockHeight = opt.GetOptimalBlockHeight();
    m_ThreadGroupSize = opt.GetOptimalThreadGroupSize();

    std::string blockWidth = std::to_string(m_BlockWidth);
    std::string blockHeight = std::to_string(m_BlockHeight);
    std::string threadGroupSize = std::to_string(m_ThreadGroupSize);

    D3D_SHADER_MACRO defines[] = {
        { "NIS_SCALER",             m_IsUpscaling ? "1" : "0" },
        { "NIS_HDR_MODE",           m_IsHDR ? "2" : "0" },
        { "NIS_BLOCK_WIDTH",        blockWidth.c_str() },
        { "NIS_BLOCK_HEIGHT",       blockHeight.c_str() },
        { "NIS_THREAD_GROUP_SIZE",  threadGroupSize.c_str() },
        { "NIS_USE_HALF_PRECISION", m_IsHalfprecision ? "1" : "0" },
        { nullptr, nullptr }
    };

    m_Hr = D3DCompile(
        hlslSource.constData(),
        hlslSource.size(),
        "NIS_Main.hlsl",
        defines,
        nullptr,
        "main",
        "cs_5_0",
        m_CompileFlags,
        0,
        &m_ShaderBlob,
        &m_ErrorBlob
        );

    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DCompile() failed: %x",
                     m_Hr);
        if (m_ErrorBlob) {
            const char* errorMsg = static_cast<const char*>(m_ErrorBlob->GetBufferPointer());
            size_t errorSize = m_ErrorBlob->GetBufferSize();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Shader compile error:\n%.*s",
                         static_cast<int>(errorSize), errorMsg);
            m_ErrorBlob->Release();
            m_ErrorBlob = nullptr;
        }

        return false;
    }

    m_Hr = m_Device->CreateComputeShader(
        m_ShaderBlob->GetBufferPointer(),
        m_ShaderBlob->GetBufferSize(),
        nullptr,
        m_ComputeShaders[0].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateComputeShader() failed: %x",
                     m_Hr);
        return false;
    }

    return true;
}

/**
 * \brief Initialize RCAS operations
 *
 * Prepare Constant, Shader, SRV, UAV
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeRCAS(uint slot)
{
    RCASConstant constantBuffer = { 0 };
    constantBuffer.Sharpness = 0.5f;
    constantBuffer.Contrast = 0.4f;
    constantBuffer.DisplayWidth = m_OutWidth;
    constantBuffer.DisplayHeight = m_OutHeight;

    createComputeShaderConstantBuffer(&constantBuffer, sizeof(constantBuffer), slot);

    QFile file(":/enhancer/rcas.hlsl");
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not open the file");
        return false;
    }
    QByteArray hlslSource = file.readAll();
    file.close();

    m_Hr = D3DCompile(
        hlslSource.constData(),
        hlslSource.size(),
        "rcas.hlsl",
        nullptr,
        nullptr,
        "CSMain",
        "cs_5_0",
        m_CompileFlags,
        0,
        &m_ShaderBlob,
        &m_ErrorBlob
        );

    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DCompile() failed: %x",
                     m_Hr);
        if (m_ErrorBlob) {
            const char* errorMsg = static_cast<const char*>(m_ErrorBlob->GetBufferPointer());
            size_t errorSize = m_ErrorBlob->GetBufferSize();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Shader compile error:\n%.*s",
                         static_cast<int>(errorSize), errorMsg);
            m_ErrorBlob->Release();
            m_ErrorBlob = nullptr;
        }
        return false;
    }

    m_Hr = m_Device->CreateComputeShader(
        m_ShaderBlob->GetBufferPointer(),
        m_ShaderBlob->GetBufferSize(),
        nullptr,
        m_ComputeShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateComputeShader() failed: %x",
                     m_Hr);
        return false;
    }

    if(slot > 0){
        // If Sharpening is Pass2, we need to get UAV from Pass1 (Upscaling)
        if(!createSRV(m_TextureUAVs[slot-1].Get(), slot)){
            return false;
        }
    } else {
        if(!createSRV(m_TextureSRVs[slot].Get(), slot)){
            return false;
        }
    }

    if(!createTexture(m_TextureUAVs[slot], m_OutWidth, m_OutHeight, false, false, true)){
        return false;
    }

    if(!createUAV(m_TextureUAVs[slot].Get(), slot)){
        return false;
    }

    return true;
}

/**
 * \brief Initialize CAS operations
 *
 * Prepare Constant, Shader, SRV, RTV
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeCAS(uint slot)
{
    CASConstant constantBuffer = {};
    constantBuffer.px = 1.0f / static_cast<float>(m_InWidth);
    constantBuffer.py = 1.0f / static_cast<float>(m_InHeight);
    constantBuffer.wh = DirectX::XMFLOAT2(static_cast<float>(m_InWidth), static_cast<float>(m_InHeight));
    constantBuffer.counter = 0;
    constantBuffer.clock = 0.0f;

    createPixelShaderConstantBuffer(&constantBuffer, sizeof(constantBuffer), slot);

    QFile file(":/enhancer/cas.hlsl");
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not open the file");
        return false;
    }
    QByteArray hlslSource = file.readAll();
    file.close();

    m_Hr = D3DCompile(
        hlslSource.constData(),
        hlslSource.size(),
        "cas.hlsl",
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        m_CompileFlags,
        0,
        &m_ShaderBlob,
        &m_ErrorBlob
        );

    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DCompile() failed: %x",
                     m_Hr);
        if (m_ErrorBlob) {
            const char* errorMsg = static_cast<const char*>(m_ErrorBlob->GetBufferPointer());
            size_t errorSize = m_ErrorBlob->GetBufferSize();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Shader compile error:\n%.*s",
                         static_cast<int>(errorSize), errorMsg);
            m_ErrorBlob->Release();
            m_ErrorBlob = nullptr;
        }
        return false;
    }

    m_Hr = m_Device->CreatePixelShader(
        m_ShaderBlob->GetBufferPointer(),
        m_ShaderBlob->GetBufferSize(),
        nullptr,
        m_PixelShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreatePixelShader() failed: %x",
                     m_Hr);
        return false;
    }

    if(!createSRV(m_TextureSRVs[slot].Get(), slot)){
        return false;
    }

    if(!createRTV(m_TextureOut.Get(), slot)){
        return false;
    }

    return true;
}

/**
 * \brief Initialize UPSCALER operations
 *
 * Prepare Constant, Shader, SRV, UAV
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeUPSCALER(uint slot)
{    
    UPSCALERConstant constantBuffer = { 0 };
    constantBuffer._SrcWidth = m_InWidth;
    constantBuffer._SrcHeight = m_InHeight;
    constantBuffer._DstWidth = m_OutWidth;
    constantBuffer._DstHeight = m_OutHeight;

    createComputeShaderConstantBuffer(&constantBuffer, sizeof(constantBuffer), slot);

    QFile file(":/enhancer/" + m_UpscalerHLSL);
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not open the file");
        return false;
    }
    QByteArray hlslSource = file.readAll();
    file.close();

    m_Hr = D3DCompile(
        hlslSource.constData(),
        hlslSource.size(),
        "Upscaler HLSL",
        nullptr,
        nullptr,
        "CSMain",
        "cs_5_0",
        m_CompileFlags,
        0,
        &m_ShaderBlob,
        &m_ErrorBlob
        );

    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DCompile() failed: %x",
                     m_Hr);
        if (m_ErrorBlob) {
            const char* errorMsg = static_cast<const char*>(m_ErrorBlob->GetBufferPointer());
            size_t errorSize = m_ErrorBlob->GetBufferSize();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Shader compile error:\n%.*s",
                         static_cast<int>(errorSize), errorMsg);
            m_ErrorBlob->Release();
            m_ErrorBlob = nullptr;
        }
        return false;
    }

    m_Hr = m_Device->CreateComputeShader(
        m_ShaderBlob->GetBufferPointer(),
        m_ShaderBlob->GetBufferSize(),
        nullptr,
        m_ComputeShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateComputeShader() failed: %x",
                     m_Hr);
        return false;
    }


    if(!createSRV(m_TextureSRVs[slot].Get(), slot)){
        return false;
    }

    // Set Set the Texture as UAV (output of EASU, Pass1) and SRV (input of RCAS, Pass2)
    if(!createTexture(m_TextureUAVs[slot], m_OutWidth, m_OutHeight, true, false, true)){
        return false;
    }

    if(!createUAV(m_TextureUAVs[slot].Get(), slot)){
        return false;
    }

    return true;
}

/**
 * \brief Initialize a Compute Pixel shader operations
 *
 * Prepare a compute shader to invert color in order to check the process
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeTESTCS(uint slot)
{
    TEXTCSConstant constantBuffer = { 0 };
    constantBuffer.Width = m_OutWidth;
    constantBuffer.Height = m_OutHeight;

    createComputeShaderConstantBuffer(&constantBuffer, sizeof(constantBuffer), slot);

    QFile file(":/enhancer/debug_cs.hlsl");
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not open the file");
        return false;
    }
    QByteArray hlslSource = file.readAll();
    file.close();

    m_Hr = D3DCompile(
        hlslSource.constData(),
        hlslSource.size(),
        "debug_cs.hlsl",
        nullptr,
        nullptr,
        "main",
        "cs_5_0",
        m_CompileFlags,
        0,
        &m_ShaderBlob,
        &m_ErrorBlob
        );

    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DCompile() failed: %x",
                     m_Hr);
        if (m_ErrorBlob) {
            const char* errorMsg = static_cast<const char*>(m_ErrorBlob->GetBufferPointer());
            size_t errorSize = m_ErrorBlob->GetBufferSize();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Shader compile error:\n%.*s",
                         static_cast<int>(errorSize), errorMsg);
            m_ErrorBlob->Release();
            m_ErrorBlob = nullptr;
        }
        return false;
    }

    m_Hr = m_Device->CreateComputeShader(
        m_ShaderBlob->GetBufferPointer(),
        m_ShaderBlob->GetBufferSize(),
        nullptr,
        m_ComputeShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateComputeShader() failed: %x",
                     m_Hr);
        return false;
    }

    if(!createSRV(m_TextureSRVs[slot].Get(), slot)){
        return false;
    }

    if(!createTexture(m_TextureUAVs[slot], m_OutWidth, m_OutHeight, false, false, true)){
        return false;
    }

    if(!createUAV(m_TextureUAVs[slot].Get(), slot)){
        return false;
    }

    return true;
}

/**
 * \brief Initialize a Compute Pixel shader operations
 *
 * Prepare a compute shader to copy the texture input into the texture output
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeCOPY(uint slot)
{
    TEXTCSConstant constantBuffer = { 0 };
    constantBuffer.Width = m_OutWidth;
    constantBuffer.Height = m_OutHeight;

    createComputeShaderConstantBuffer(&constantBuffer, sizeof(constantBuffer), slot);

    QFile file(":/enhancer/copy.hlsl");
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not open the file");
        return false;
    }
    QByteArray hlslSource = file.readAll();
    file.close();

    m_Hr = D3DCompile(
        hlslSource.constData(),
        hlslSource.size(),
        "copy.hlsl",
        nullptr,
        nullptr,
        "main",
        "cs_5_0",
        m_CompileFlags,
        0,
        &m_ShaderBlob,
        &m_ErrorBlob
        );

    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DCompile() failed: %x",
                     m_Hr);
        if (m_ErrorBlob) {
            const char* errorMsg = static_cast<const char*>(m_ErrorBlob->GetBufferPointer());
            size_t errorSize = m_ErrorBlob->GetBufferSize();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Shader compile error:\n%.*s",
                         static_cast<int>(errorSize), errorMsg);
            m_ErrorBlob->Release();
            m_ErrorBlob = nullptr;
        }
        return false;
    }

    m_Hr = m_Device->CreateComputeShader(
        m_ShaderBlob->GetBufferPointer(),
        m_ShaderBlob->GetBufferSize(),
        nullptr,
        m_ComputeShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateComputeShader() failed: %x",
                     m_Hr);
        return false;
    }

    if(!createSRV(m_TextureSRVs[slot].Get(), slot)){
        return false;
    }

    if(!createTexture(m_TextureUAVs[slot], m_OutWidth, m_OutHeight, false, false, true)){
        return false;
    }

    if(!createUAV(m_TextureUAVs[slot].Get(), slot)){
        return false;
    }

    return true;
}


/**
 * \brief Initialize a Simple Pixel shader operations
 *
 * Prepare a pixel shader to invert color in order to check the process
 *
 * \param in slot, Index of the object array
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::initializeTESTPS(uint slot)
{
    QFile file(":/enhancer/debug_ps.hlsl");
    if (!file.open(QIODevice::ReadOnly)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not open the file");
        return false;
    }
    QByteArray hlslSource = file.readAll();
    file.close();

    m_Hr = D3DCompile(
        hlslSource.constData(),
        hlslSource.size(),
        "debug_ps.hlsl",
        nullptr,
        nullptr,
        "main",
        "ps_5_0",
        m_CompileFlags,
        0,
        &m_ShaderBlob,
        &m_ErrorBlob
        );

    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "D3DCompile() failed: %x",
                     m_Hr);
        if (m_ErrorBlob) {
            const char* errorMsg = static_cast<const char*>(m_ErrorBlob->GetBufferPointer());
            size_t errorSize = m_ErrorBlob->GetBufferSize();
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Shader compile error:\n%.*s",
                         static_cast<int>(errorSize), errorMsg);
            m_ErrorBlob->Release();
            m_ErrorBlob = nullptr;
        }
        return false;
    }

    m_Hr = m_Device->CreatePixelShader(
        m_ShaderBlob->GetBufferPointer(),
        m_ShaderBlob->GetBufferSize(),
        nullptr,
        m_PixelShaders[slot].GetAddressOf()
        );
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreatePixelShader() failed: %x",
                     m_Hr);
        return false;
    }

    if(!createSRV(m_TextureSRVs[slot].Get(), slot)){
        return false;
    }

    if(!createRTV(m_TextureOut.Get(), slot)){
        return false;
    }

    return true;
}

/**
 * \brief Apply FSR1 shader operations
 *
 * Apply FSR1 shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyFSR1()
{
    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    static const int threadGroupWorkRegionDim = 16;
    int dispatchX = (m_OutWidth + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;
    int dispatchY = (m_OutHeight + (threadGroupWorkRegionDim - 1)) / threadGroupWorkRegionDim;

    // PASS1: APPLY FSR1 EASU (UPSCALE)

    // Unbind SRVs and PS from previous frame
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Bind SRV and compute shader for this draw
    m_DeviceContext->CSSetShader(m_ComputeShaders[0].Get(), nullptr, 0); // Bind CS
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[0].GetAddressOf()); // b0
    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf()); // t0
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf()); // s0
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[0].GetAddressOf(), nullptr); // u0

    // Process shaders on the output texture
    // [numthreads(64, 1, 1)]
    m_DeviceContext->Dispatch(dispatchX, dispatchY, 1);

    if(!m_ComputeShaders[1]){
        // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
        m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[0].Get(), 0, &m_DestBox);

        return true;
    }

    // PASS2: APPLY FSR1 RCAS (SHARPENING)

    // Unbind SRVs and PS from previous frame
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Bind SRV and compute shader for this draw
    m_DeviceContext->CSSetShader(m_ComputeShaders[1].Get(), nullptr, 0); // Bind CS
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[1].GetAddressOf()); // b0
    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[1].GetAddressOf()); // t0
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf()); // s0
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[1].GetAddressOf(), nullptr); // u0

    // Process shaders on the output texture
    // [numthreads(64, 1, 1)]
    m_DeviceContext->Dispatch(dispatchX, dispatchY, 1);

    // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
    m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[1].Get(), 0, &m_DestBox);

    return true;
}

/**
 * \brief Apply NIS shader operations
 *
 * Apply NIS shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyNIS()
{
    // Unbind SRVs and PS from previous frame
    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf());
    if(m_IsUpscaling){
        m_DeviceContext->CSSetShaderResources(1, 1, m_SRVs[1].GetAddressOf());
        m_DeviceContext->CSSetShaderResources(2, 1, m_SRVs[2].GetAddressOf());
    }
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[0].GetAddressOf(), nullptr);
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf());
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[0].GetAddressOf());
    m_DeviceContext->CSSetShader(m_ComputeShaders[0].Get(), nullptr, 0);

    m_DeviceContext->Dispatch(
        uint(std::ceil(m_OutWidth / float(m_BlockWidth))),
        uint(std::ceil(m_OutHeight / float(m_BlockHeight))),
        1
        );

    // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
    m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[0].Get(), 0, &m_DestBox);

    return true;
}

/**
 * \brief Apply RCAS shader operations
 *
 * Apply RCAS shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyRCAS()
{
    // Unbind SRVs and PS from previous frame
    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Bind SRV and compute shader for this draw
    m_DeviceContext->CSSetShader(m_ComputeShaders[0].Get(), nullptr, 0); // Bind CS
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[0].GetAddressOf()); // b0
    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf()); // t0
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf()); // s0
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[0].GetAddressOf(), nullptr); // u0

    // Process shaders on the output texture
    // [numthreads(32, 32, 1)]
    m_DeviceContext->Dispatch(uint(std::ceil(m_OutWidth / float(32))), uint(std::ceil(m_OutHeight / float(32))), 1);

    // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
    m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[0].Get(), 0, &m_DestBox);

    return true;
}

/**
 * \brief Apply CAS shader operations
 *
 * Apply CAS shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyCAS()
{
    // Unbind SRVs and PS from previous frame
    ID3D11ShaderResourceView* nullSrvs[1] = { nullptr };
    m_DeviceContext->PSSetShaderResources(0, 1, nullSrvs);
    m_DeviceContext->PSSetShader(nullptr, nullptr, 0);

    // Bind SRV and pixel shader for this draw
    m_DeviceContext->OMSetRenderTargets(1, m_RTVs[0].GetAddressOf(), nullptr);
    m_DeviceContext->PSSetShader(m_PixelShaders[0].Get(), nullptr, 0);
    m_DeviceContext->PSSetConstantBuffers(0, 1, m_PSConstantBuffers[0].GetAddressOf());
    m_DeviceContext->PSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf());
    m_DeviceContext->PSSetSamplers(0, 1, m_Sampler.GetAddressOf());

    // Process shaders on the output texture
    m_DeviceContext->DrawIndexed(6, 0, 0);

    return true;
}

/**
 * \brief Apply UPSCALER shader operations
 *
 * Apply UPSCALER shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyUPSCALER()
{
    // PASS1: APPLY UPSCALER

    // Unbind SRVs and PS from previous frame
    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Bind SRV and compute shader for this draw
    m_DeviceContext->CSSetShader(m_ComputeShaders[0].Get(), nullptr, 0); // Bind CS
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[0].GetAddressOf()); // b0
    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf()); // t0
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf()); // s0
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[0].GetAddressOf(), nullptr); // u0

    // Process shaders on the output texture
    // [numthreads(16, 16, 1)]
    m_DeviceContext->Dispatch(uint(std::ceil(m_OutWidth / float(16))), uint(std::ceil(m_OutHeight / float(16))), 1);

    if(!m_ComputeShaders[1]){
        // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
        m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[0].Get(), 0, &m_DestBox);

        return true;
    }

    // (if specified only) PASS2: APPLY RCAS (SHARPENING)

    // Unbind SRVs and PS from previous frame
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Bind SRV and compute shader for this draw
    m_DeviceContext->CSSetShader(m_ComputeShaders[1].Get(), nullptr, 0); // Bind CS
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[1].GetAddressOf()); // b0
    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[1].GetAddressOf()); // t0
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf()); // s0
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[1].GetAddressOf(), nullptr); // u0

    // Process shaders on the output texture
    // [numthreads(64, 1, 1)]
    m_DeviceContext->Dispatch(uint(std::ceil(m_OutWidth / float(16))), uint(std::ceil(m_OutHeight / float(16))), 1);

    // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
    m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[1].Get(), 0, &m_DestBox);

    return true;
}

/**
 * \brief Apply COPY shader operations
 *
 * Apply COPY shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyCOPY()
{
    // Unbind SRVs and PS from previous frame
    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Bind SRV and compute shader for this draw
    m_DeviceContext->CSSetShader(m_ComputeShaders[0].Get(), nullptr, 0); // Bind CS
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[0].GetAddressOf()); // b0
    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf()); // t0
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf()); // s0
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[0].GetAddressOf(), nullptr); // u0

    // Process shaders on the output texture
    // [numthreads(16, 16, 1)]
    m_DeviceContext->Dispatch(uint(std::ceil(m_OutWidth / float(16))), uint(std::ceil(m_OutHeight / float(16))), 1);

    // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
    m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[0].Get(), 0, &m_DestBox);

    return true;
}

/**
 * \brief Apply TESTCS shader operations
 *
 * Apply TESTCS shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyTESTCS()
{
    // Unbind SRVs and PS from previous frame
    ID3D11ShaderResourceView* nullSRVs[1] = { nullptr };
    m_DeviceContext->CSSetShaderResources(0, 1, nullSRVs);
    ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    // Bind SRV and compute shader for this draw
    m_DeviceContext->CSSetShader(m_ComputeShaders[0].Get(), nullptr, 0); // Bind CS
    m_DeviceContext->CSSetConstantBuffers(0, 1, m_CSConstantBuffers[0].GetAddressOf()); // b0
    m_DeviceContext->CSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf()); // t0
    m_DeviceContext->CSSetSamplers(0, 1, m_Sampler.GetAddressOf()); // s0
    m_DeviceContext->CSSetUnorderedAccessViews(0, 1, m_UAVs[0].GetAddressOf(), nullptr); // u0

    // Process shaders on the output texture
    // [numthreads(16, 16, 1)]
    m_DeviceContext->Dispatch(uint(std::ceil(m_OutWidth / float(16))), uint(std::ceil(m_OutHeight / float(16))), 1);

    // BackBuffer doesn't support D3D11_BIND_UNORDERED_ACCESS flag, so we use an intermediate UAV Texture
    m_DeviceContext->CopySubresourceRegion(m_TextureOut.Get(), 0, m_OffsetLeft, m_OffsetTop, 0, m_TextureUAVs[0].Get(), 0, &m_DestBox);

    // setTextureTest(m_TextureUAVs[0].Get());
    // copyTextureTest("TestComputeShader.png");

    return true;
}

/**
 * \brief Apply TESTPS shader operations
 *
 * Apply TESTPS shader operations
 *
 * \return bool, returns True if successful
 */
bool D3D11VAShaders::applyTESTPS()
{
    // Unbind SRVs and PS from previous frame
    ID3D11ShaderResourceView* nullSrvs[1] = { nullptr };
    m_DeviceContext->PSSetShaderResources(0, 1, nullSrvs);
    m_DeviceContext->PSSetShader(nullptr, nullptr, 0);

    // Bind SRV and pixel shader for this draw
    m_DeviceContext->OMSetRenderTargets(1, m_RTVs[0].GetAddressOf(), nullptr);
    m_DeviceContext->PSSetShader(m_PixelShaders[0].Get(), nullptr, 0);
    m_DeviceContext->PSSetShaderResources(0, 1, m_SRVs[0].GetAddressOf());
    m_DeviceContext->PSSetSamplers(0, 1, m_Sampler.GetAddressOf());

    // Process shaders on the output texture
    m_DeviceContext->DrawIndexed(6, 0, 0);

    // setTextureTest(m_TextureRTVs[0].Get());
    // copyTextureTest("TestCPixelShader.png");

    return true;
}

/**
 * \brief Create a Texture used for Debugging purpose only
 *
 * Create a Texture used for Debugging purpose only
 *
 * \param in texture, Texture to get description from
 * \return bool, returns True if successful
 */
void D3D11VAShaders::setTextureTest(ID3D11Texture2D* texture)
{
#ifdef QT_DEBUG
    m_TextureTestSource = texture;
    D3D11_TEXTURE2D_DESC textDesc = {};
    texture->GetDesc(&textDesc);

    // Debug specifics
    textDesc.Usage = D3D11_USAGE_STAGING;
    textDesc.BindFlags = 0;
    textDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    textDesc.MiscFlags = 0;
    m_Hr = m_Device->CreateTexture2D(&textDesc, nullptr, m_TextureTestDest.GetAddressOf());
    if (FAILED(m_Hr)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ID3D11Device::CreateTexture2D() failed: %x",
                     m_Hr);
        return;
    }
#endif
}

/**
 * \brief Create a Texture from a Ressource, used for Debugging purpose only
 *
 * Create a Texture from a Ressource, used for Debugging purpose only
 *
 * \param in texture, Texture variable to instantiate
 * \return bool, returns True if successful
 */
void D3D11VAShaders::setTextureTest(ID3D11Resource* resource)
{
#ifdef QT_DEBUG
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    resource->QueryInterface(IID_PPV_ARGS(&texture));
    setTextureTest(texture.Get());
#endif
}

/**
 * \brief Copy the texture into a PNG file
 *
 * Copy the texture into a PNG file
 *
 * \param in imageName, File name to generate
 * \return bool, returns True if successful
 */
void D3D11VAShaders::copyTextureTest(QString imageName)
{
#ifdef QT_DEBUG
    D3D11_TEXTURE2D_DESC desc;
    D3D11_MAPPED_SUBRESOURCE mapped;
    m_DeviceContext->CopyResource(m_TextureTestDest.Get(), m_TextureTestSource.Get());
    desc = {};
    m_TextureTestSource->GetDesc(&desc);
    mapped = {};
    m_DeviceContext->Map(m_TextureTestDest.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    QImage imageRGB((const uchar*)mapped.pData, desc.Width, desc.Height, mapped.RowPitch, QImage::Format_RGBA8888);
    imageRGB.save(imageName);
    m_DeviceContext->Unmap(m_TextureTestDest.Get(), 0);
#endif
}

// Following methods are specific to NIS

/**
 * \brief Create NIS SRV
 *
 * Create NIS SRV
 *
 * \return void
 */
void D3D11VAShaders::NIScreateSRV(ID3D11Resource* pResource, DXGI_FORMAT format, ID3D11ShaderResourceView** ppSRView)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
    ZeroMemory(&srvDesc, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));
    srvDesc.Format = format;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;
    m_Device->CreateShaderResourceView(pResource, &srvDesc, ppSRView);
}

/**
 * \brief Create NIS Texture
 *
 * Create NIS Texture
 *
 * \return void
 */
void D3D11VAShaders::NIScreateTexture2D(int w, int h, DXGI_FORMAT format, D3D11_USAGE heapType, const void* data, uint32_t rowPitch, uint32_t imageSize, ID3D11Texture2D** ppTexture2D)
{
    D3D11_TEXTURE2D_DESC desc;
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;

    desc.MiscFlags = 0;
    desc.Usage = heapType;
    if (heapType == D3D11_USAGE_STAGING)
    {
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ | D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags = 0;
    }
    else
    {
        desc.CPUAccessFlags = 0;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    D3D11_SUBRESOURCE_DATA* pInitialData = nullptr;
    D3D11_SUBRESOURCE_DATA initData;
    if (data)
    {
        initData.pSysMem = data;
        initData.SysMemPitch = rowPitch;
        initData.SysMemSlicePitch = imageSize;
        pInitialData = &initData;
    }

    m_Device->CreateTexture2D(&desc, pInitialData, ppTexture2D);
}

/**
 * \brief Get NIS Texture Data
 *
 * Get NIS Texture Data
 *
 * \return void
 */
void D3D11VAShaders::NISgetTextureData(ID3D11Texture2D* texture, std::vector<uint8_t>& data, uint32_t& width, uint32_t& height, uint32_t& rowPitch)
{
    D3D11_TEXTURE2D_DESC desc;
    texture->GetDesc(&desc);
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.Usage = D3D11_USAGE_STAGING;
    ComPtr<ID3D11Texture2D> stage;
    m_Device->CreateTexture2D(&desc, nullptr, &stage);
    m_DeviceContext->CopyResource(stage.Get(), texture);
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    m_DeviceContext->Map(stage.Get(), 0, D3D11_MAP_READ, 0, &mappedResource);
    uint8_t* mappData = (uint8_t*)mappedResource.pData;
    width = desc.Width;
    height = desc.Height;
    rowPitch = mappedResource.RowPitch;
    data.resize(mappedResource.DepthPitch);
    memcpy(data.data(), mappData, mappedResource.DepthPitch);
    m_DeviceContext->Unmap(stage.Get(), 0);
}

/**
 * \brief Create NIS Constant Buffer
 *
 * Create NIS Constant Buffer
 *
 * \return void
 */
void D3D11VAShaders::NIScreateConstBuffer(void* initialData, uint32_t size, ID3D11Buffer** ppBuffer)
{
    D3D11_BUFFER_DESC bDesc;
    bDesc.ByteWidth = size;
    bDesc.Usage = D3D11_USAGE_DYNAMIC;
    bDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    bDesc.MiscFlags = 0;
    bDesc.StructureByteStride = 0;

    D3D11_SUBRESOURCE_DATA srData;
    srData.pSysMem = initialData;
    m_Device->CreateBuffer(&bDesc, &srData, ppBuffer);
}

/**
 * \brief Update NIS Constant Buffer
 *
 * Update NIS Constant Buffer
 *
 * \return void
 */
void D3D11VAShaders::NISupdateConstBuffer(void* data, uint32_t size, ID3D11Buffer* ppBuffer)
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    m_DeviceContext->Map(ppBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    uint8_t* mappData = (uint8_t*)mappedResource.pData;
    memcpy(mappData, data, size);
    m_DeviceContext->Unmap(ppBuffer, 0);
}
