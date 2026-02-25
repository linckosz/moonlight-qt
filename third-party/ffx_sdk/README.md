# FidelityFX SDK – Build Instructions

This project uses **FidelityFX SDK v1.1.4**.
Source: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK/tree/v1.1.4

---

## 1. Build the SDK (Windows)

A build script `BuildFidelityFXSDKvkX64.bat` is provided in this folder.

1. Copy `BuildFidelityFXSDKvkX64.bat` into the root of the FidelityFX SDK repository (`FidelityFX-SDK-v1.1.4/sdk/`)
2. Run it:
   ```bat
   BuildFidelityFXSDKvkX64.bat
   ```

---

## 2. Copy the compiled libraries

Once the build completes, copy the folder `FidelityFX-SDK-v1.1.4/sdk/build/src/backends/shaders` output folder and copy them into `ffx_sdk/` in this repository:
You shall then see file such as
```
ffx_sdk/shaders/vk/ffx_fsr1_easu_pass_permutations.h
etc.
```

---

## 3. Bug fix

Commented out "fpSwapChainConfigureFrameGeneration" is part of the Frame Interpolation feature which is not used in this FSR1-only build.
The actual implementation lives in FrameInterpolationSwapchainVK.cpp which contains compiler bugs unrelated to FSR1.
We created a customed file ffx_sdk/custom/ffx_vk.cpp avoiding the call to the Frame Interpolation feature.

```
// backendInterface->fpSwapChainConfigureFrameGeneration = ffxSetFrameGenerationConfigToSwapchainVK;
```

---
