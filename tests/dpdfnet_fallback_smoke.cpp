#include "dsp/pipeline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <string>

std::atomic<float> g_gain{1.0f};
std::atomic<bool> g_eqEnabled{false};
std::atomic<float> g_eqPresence{0.0f};
std::atomic<float> g_eqBassCut{0.0f};
std::atomic<bool> g_compressorEnabled{false};
std::atomic<bool> g_nrEnabled{true};
std::atomic<float> g_nrStrength{0.6f};
std::atomic<int> g_denoiseBackend{
    static_cast<int>(DenoiseBackendKind::Dpdfnet)};
std::atomic<uint64_t> g_denoiseResetEpoch{1};
std::atomic<bool> g_dpdfnetAvailable{false};
std::atomic<bool> g_dpdfnetDegraded{false};
std::atomic<int> g_denoiseEffectiveBackend{
    static_cast<int>(DenoiseBackendKind::Rnnoise)};

static std::wstring utf8ToWide(const char* value) {
    if (!value || !*value) return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value, -1, nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value, -1, result.data(), length) <= 0) {
        return {};
    }
    result.resize(static_cast<size_t>(length - 1));
    return result;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Usage: dpdfnet_fallback_smoke <runtime-directory> <missing-model-path>\n");
        return 2;
    }

    const std::wstring runtimeDirectory = utf8ToWide(argv[1]);
    const std::wstring missingModelPath = utf8ToWide(argv[2]);
    if (runtimeDirectory.empty() || missingModelPath.empty()) {
        std::printf("ERROR: path conversion failed\n");
        return 2;
    }

    DspPipeline pipeline;
    if (!pipeline.init(48000.0f, runtimeDirectory, missingModelPath)) {
        std::printf("ERROR: RNNoise pipeline failed to initialize\n");
        return 1;
    }

    float samples[480]{};
    for (int i = 0; i < 480; ++i) {
        samples[i] = 0.05f * std::sin(
            2.0f * 3.14159265358979323846f * 220.0f * i / 48000.0f);
    }
    pipeline.process(samples, 480, 48000.0f);

    const int effective = g_denoiseEffectiveBackend.load(
        std::memory_order_acquire);
    if (g_dpdfnetAvailable.load(std::memory_order_acquire) ||
        effective != static_cast<int>(DenoiseBackendKind::Rnnoise)) {
        std::printf("ERROR: missing DPDFNet resources did not fall back to RNNoise\n");
        return 1;
    }
    for (float sample : samples) {
        if (!std::isfinite(sample)) {
            std::printf("ERROR: fallback output is non-finite\n");
            return 1;
        }
    }

    std::printf("DPDFNet fallback smoke OK: effective=RNNoise\n");
    return 0;
}
