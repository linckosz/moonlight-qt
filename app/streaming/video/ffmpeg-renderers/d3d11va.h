#pragma once

#include "renderer.h"

#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <CGuid.h>
#include <atlbase.h>
#include <QFuture>
#include "d3d11va_shaders.h"
#include "streaming/video/videoenhancement.h"
#include "settings/streamingpreferences.h"
#include "public/common/AMFFactory.h"

extern "C" {
#include <libavutil/hwcontext_d3d11va.h>
}

#include <wrl/client.h>

class D3D11VARenderer : public IFFmpegRenderer, QObject
{
public:
    D3D11VARenderer(int decoderSelectionPass);
    virtual ~D3D11VARenderer() override;
    virtual bool initialize(PDECODER_PARAMETERS params) override;
    virtual bool prepareDecoderContext(AVCodecContext* context, AVDictionary**) override;
    virtual bool prepareDecoderContextInGetFormat(AVCodecContext* context, AVPixelFormat pixelFormat) override;
    virtual void renderFrame(AVFrame* frame) override;
    virtual void notifyOverlayUpdated(Overlay::OverlayType) override;
    virtual int getRendererAttributes() override;
    virtual int getDecoderCapabilities() override;
    virtual bool needsTestFrame() override;
    virtual void setHdrMode(bool enabled) override;
    virtual InitFailureReason getInitFailureReason() override;

    enum PixelShaders {
        GENERIC_YUV_420,
        BT_601_LIMITED_YUV_420,
        BT_2020_LIMITED_YUV_420,
        GENERIC_AYUV,
        GENERIC_Y410,
        _COUNT
    };

    struct TextureInfo {
        int width;
        int height;
        int left;
        int top;
    };

private:
    static void lockContext(void* lock_ctx);
    static void unlockContext(void* lock_ctx);

    bool setupRenderingResources();
    bool setupAmfTexture();
    std::vector<DXGI_FORMAT> getVideoTextureSRVFormats();
    bool setupVideoTexture(); // for !m_BindDecoderOutputTextures
    bool setupTexturePoolViews(AVD3D11VAFramesContext* frameContext); // for m_BindDecoderOutputTextures
    bool setupEnhancedTexture();
    void renderOverlay(Overlay::OverlayType type);
    void bindColorConversion(AVFrame* frame);
    void prepareEnhancedOutput(AVFrame* frame);
    void renderVideo(AVFrame* frame);
    bool createVideoProcessor();
    bool initializeVideoProcessor();
    void enhanceAutoSelection(DXGI_ADAPTER_DESC1* adapterDesc);
    bool enableAMDVideoSuperResolution(bool activate = true, bool logInfo = true);
    bool enableIntelVideoSuperResolution(bool activate = true, bool logInfo = true);
    bool enableNvidiaVideoSuperResolution(bool activate = true, bool logInfo = true);
    bool enableAMDHDR(bool activate = true, bool logInfo = true);
    bool enableIntelHDR(bool activate = true, bool logInfo = true);
    bool enableNvidiaHDR(bool activate = true, bool logInfo = true);
    bool isNvidiaRTXorNewer();
    bool checkDecoderSupport(IDXGIAdapter* adapter);
    int getAdapterIndexByEnhancementCapabilities();
    bool getDisplayHDRStatus();
    void updateDisplayHDRStatusAsync();
    bool createDeviceByAdapterIndex(int adapterIndex, bool* adapterNotFound = nullptr);

    // Available in Debug mode only, turn it true to see time consumed by the GPU to draw.
    // It doesn't mean it is the real compute time, some like Nvidia may adjust their time
    // according to the refresh rate.
    bool m_QtDebugWaitForGPUFence = false; // (Default: false) Change to true to see the time consumed by the GPU before rendering the texture
    Microsoft::WRL::ComPtr<ID3D11Query> m_GpuEventQuery = nullptr;

    StreamingPreferences* m_Preferences;
    int m_DecoderSelectionPass;
    int m_DevicesWithFL11Support;
    int m_DevicesWithCodecSupport;

    int m_AdapterIndex = 0;
    int m_OutputIndex = 0;

    enum class SupportedFenceType {
        None,
        NonMonitored,
        Monitored,
    };

    Microsoft::WRL::ComPtr<IDXGIFactory5> m_Factory;
    Microsoft::WRL::ComPtr<ID3D11Device> m_Device;
    Microsoft::WRL::ComPtr<IDXGISwapChain4> m_SwapChain;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_DeviceContext;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
    SupportedFenceType m_FenceType;
    SDL_mutex* m_ContextLock;
    bool m_BindDecoderOutputTextures;
    bool m_IsDisplayHDRenabled = false;
    bool m_cancelHDRUpdate = false;
    QFuture<void> m_HDRUpdateFuture;
    D3D11_BOX m_SrcBox;
    D3D11_BOX m_DestBox;

    Microsoft::WRL::ComPtr<ID3D11VideoDevice> m_VideoDevice;
    Microsoft::WRL::ComPtr<ID3D11VideoContext2> m_VideoContext;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_VideoProcessorExt;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_VideoProcessor;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessor> m_VideoProcessorTone;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_VideoProcessorEnumeratorExt;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_VideoProcessorEnumerator;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorEnumerator> m_VideoProcessorEnumeratorTone;
    D3D11_VIDEO_PROCESSOR_CAPS m_VideoProcessorCapabilities;
    D3D11_VIDEO_PROCESSOR_STREAM m_StreamDataExt;
    D3D11_VIDEO_PROCESSOR_STREAM m_StreamData;
    D3D11_VIDEO_PROCESSOR_STREAM m_StreamDataTone;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> m_OutputViewExt;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> m_OutputView;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorOutputView> m_OutputViewTone;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> m_InputViewExt;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> m_InputView;
    Microsoft::WRL::ComPtr<ID3D11VideoProcessorInputView> m_InputViewTone;
    DXGI_COLOR_SPACE_TYPE m_InputColorSpaceExt = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    DXGI_COLOR_SPACE_TYPE m_OutputColorSpaceExt = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    DXGI_COLOR_SPACE_TYPE m_InputColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    DXGI_COLOR_SPACE_TYPE m_OutputColorSpace = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    DXGI_COLOR_SPACE_TYPE m_InputColorSpaceTone = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
    DXGI_COLOR_SPACE_TYPE m_OutputColorSpaceTone = DXGI_COLOR_SPACE_YCBCR_STUDIO_G22_LEFT_P709;
    bool m_IsBgColorYCbCrExt = false;
    bool m_IsBgColorYCbCr = false;
    bool m_IsBgColorYCbCrTone = false;
    // Default rects to allow VSR detection; actual values set at runtime
    RECT m_SourceRectExt = { 0, 0, 1280, 720 };
    RECT m_DestRectExt = { 0, 0, 1920, 1080 };
    RECT m_TargetRectExt = { 0, 0, 1920, 1080 };
    RECT m_SourceRect = { 0, 0, 1920, 1080 };
    RECT m_DestRect = { 0, 0, 1920, 1080 };
    RECT m_TargetRect = { 0, 0, 1920, 1080 };
    RECT m_SourceRectTone = { 0, 0, 1920, 1080 };
    RECT m_DestRectTone = { 0, 0, 1920, 1080 };
    RECT m_TargetRectTone = { 0, 0, 1920, 1080 };
    Microsoft::WRL::ComPtr<ID3D11Resource> m_BackBufferResource;
    AVD3D11VAFramesContext* m_D3d11vaFramesContext;
    VideoEnhancement* m_VideoEnhancement;
    bool m_2PassVideoProcessor = false;
    bool m_HDRToneMapping = false;
    bool m_AutoStreamSuperResolution = false;
    bool m_UseFenceHack;
    bool m_IsIntegratedGPU = false;
    bool m_VendorVSRenabled = false;
    bool m_VendorHDRenabled = false;

    DECODER_PARAMETERS m_DecoderParams;
    bool m_IsDecoderHDR = false;
    bool m_yuv444 = false;
    int m_TextureAlignment;
    DXGI_FORMAT m_TextureFormat;
    int m_DisplayWidth;
    int m_DisplayHeight;
    int m_LastColorSpace;
    bool m_LastFullRange;
    int m_LastColorSpaceE;
    bool m_LastFullRangeE;
    bool m_FirstFrameE = true;
    AVColorTransferCharacteristic m_LastColorTrc;
    DXGI_HDR_METADATA_HDR10 m_StreamHDRMetaData;
    DXGI_HDR_METADATA_HDR10 m_OutputHDRMetaData;

    bool m_AllowTearing;

    std::array<Microsoft::WRL::ComPtr<ID3D11PixelShader>, PixelShaders::_COUNT> m_VideoPixelShaders;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_VideoVertexBuffer;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_AmfTexture;
    // Only valid if !m_BindDecoderOutputTextures
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_VideoTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_VPExtensionTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_VPEnhancedTexture;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> m_VPToneTexture;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC m_InputViewDescExt;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC m_InputViewDesc;
    D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC m_InputViewDescTone;

    // Only index 0 is valid if !m_BindDecoderOutputTextures
#define DECODER_BUFFER_POOL_SIZE 17
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, 2>, DECODER_BUFFER_POOL_SIZE> m_VideoTextureResourceViews;

    TextureInfo m_OutputTexture;

    SDL_SpinLock m_OverlayLock;
    std::array<Microsoft::WRL::ComPtr<ID3D11Buffer>, Overlay::OverlayMax> m_OverlayVertexBuffers;
    std::array<Microsoft::WRL::ComPtr<ID3D11Texture2D>, Overlay::OverlayMax> m_OverlayTextures;
    std::array<Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>, Overlay::OverlayMax> m_OverlayTextureResourceViews;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_OverlayPixelShader;

    AVBufferRef* m_HwDeviceContext;
    AVBufferRef* m_HwFramesContext;

    // AMD (AMF)
    amf::AMFContextPtr m_AmfContext;
    amf::AMFSurfacePtr m_AmfSurfaceIn;
    amf::AMFSurfacePtr m_AmfSurfaceOutRGB;
    amf::AMFSurfacePtr m_AmfSurfaceOutYUV;
    amf::AMFDataPtr m_AmfData;
    // amf::AMFComponentPtr does not work for m_AmfUpScaler, have to use raw pointer
    amf::AMFComponent* m_AmfUpScaler;
    amf::AMFComponentPtr m_AmfVideoConverter;
    bool m_AmfInitialized = false;
    bool m_AmfUpScalerSharpness = false;
    amf::AMF_SURFACE_FORMAT m_AmfUpScalerSurfaceFormat;
    amf::AMF_SURFACE_FORMAT m_AmfConverterSurfaceFormat;

    // Shaders class
    std::unique_ptr<D3D11VAShaders> m_Shaders = nullptr;
    D3D11VAShaders::Enhancer m_EnhancerType = D3D11VAShaders::Enhancer::NONE;
};
