#pragma once

// Dynamic binding for the optional sherpa-onnx C API.
//
// VoxMic resolves every entry point at runtime instead of linking an import
// library, so the DPDFNet payload stays genuinely optional: the executable
// starts, and runs on RNNoise, with no sherpa-onnx DLL present at all.
//
// Extracted from dpdfnet_processor.cpp, where the loader and the
// session/worker logic had grown into the same file.

#include <cstdint>

#if VOXMIC_ENABLE_DPDFNET

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <sherpa-onnx/c-api/c-api.h>

// The public header supplies the exact pinned C ABI layout.  VoxMic still
// resolves every function dynamically, so optional DLLs are never loader
// dependencies of the main executable.
struct SherpaOnnxApi {
    using CreateFn = const SherpaOnnxOnlineSpeechDenoiser* (__cdecl*)(
        const SherpaOnnxOnlineSpeechDenoiserConfig*);
    using DestroyFn = void (__cdecl*)(const SherpaOnnxOnlineSpeechDenoiser*);
    using GetSampleRateFn = int32_t (__cdecl*)(
        const SherpaOnnxOnlineSpeechDenoiser*);
    using GetFrameShiftFn = int32_t (__cdecl*)(
        const SherpaOnnxOnlineSpeechDenoiser*);
    using RunFn = const SherpaOnnxDenoisedAudio* (__cdecl*)(
        const SherpaOnnxOnlineSpeechDenoiser*, const float*, int32_t, int32_t);
    using ResetFn = void (__cdecl*)(const SherpaOnnxOnlineSpeechDenoiser*);
    using DestroyAudioFn = void (__cdecl*)(const SherpaOnnxDenoisedAudio*);

    HMODULE module = nullptr;
    CreateFn create = nullptr;
    DestroyFn destroy = nullptr;
    GetSampleRateFn getSampleRate = nullptr;
    GetFrameShiftFn getFrameShift = nullptr;
    RunFn run = nullptr;
    ResetFn reset = nullptr;
    DestroyAudioFn destroyAudio = nullptr;

    void unload() {
        if (module) {
            FreeLibrary(module);
            module = nullptr;
        }
        create = nullptr;
        destroy = nullptr;
        getSampleRate = nullptr;
        getFrameShift = nullptr;
        run = nullptr;
        reset = nullptr;
        destroyAudio = nullptr;
    }
};

#endif // VOXMIC_ENABLE_DPDFNET
