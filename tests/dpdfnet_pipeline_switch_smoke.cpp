#include "dsp/pipeline.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
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

static void fillInput(float* samples, int block, float amplitude = 0.08f) {
    for (int i = 0; i < DPDFNET_BLOCK_SAMPLES; ++i) {
        const double t = (block * DPDFNET_BLOCK_SAMPLES + i) / 48000.0;
        samples[i] = static_cast<float>(
            amplitude * std::sin(2.0 * 3.14159265358979323846 * 220.0 * t) +
            0.01 * std::sin(2.0 * 3.14159265358979323846 * 3100.0 * t));
    }
}

static bool processOne(DspPipeline& pipeline, int block, unsigned int sleepMs) {
    float samples[DPDFNET_BLOCK_SAMPLES];
    fillInput(samples, block);
    pipeline.process(samples, DPDFNET_BLOCK_SAMPLES, 48000.0f);
    for (float sample : samples) {
        if (!std::isfinite(sample)) return false;
    }
    if (sleepMs > 0) Sleep(sleepMs);
    return true;
}

static bool runDpdfnetEpoch(DspPipeline& pipeline, int& block,
    int blocks, unsigned int sleepMs, int minimumActiveBlocks) {
    int activeBlocks = 0;
    for (int i = 0; i < blocks; ++i, ++block) {
        if (!processOne(pipeline, block, sleepMs)) return false;
        if (g_denoiseEffectiveBackend.load(std::memory_order_acquire) ==
            static_cast<int>(DenoiseBackendKind::Dpdfnet)) {
            ++activeBlocks;
        }
    }
    return activeBlocks >= minimumActiveBlocks;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Usage: dpdfnet_pipeline_switch_smoke <runtime-directory> <model-path>\n");
        return 2;
    }

    const std::wstring runtimeDirectory = utf8ToWide(argv[1]);
    const std::wstring modelPath = utf8ToWide(argv[2]);
    if (runtimeDirectory.empty() || modelPath.empty()) {
        std::printf("ERROR: path conversion failed\n");
        return 2;
    }

    DspPipeline pipeline;
    if (!pipeline.init(48000.0f, runtimeDirectory, modelPath) ||
        !g_dpdfnetAvailable.load(std::memory_order_acquire)) {
        std::printf("ERROR: DPDFNet pipeline initialization failed\n");
        return 1;
    }

    int block = 0;
    if (!runDpdfnetEpoch(pipeline, block, 70, 10, 55)) {
        std::printf("ERROR: DPDFNet did not remain active during the initial epoch\n");
        return 1;
    }

    g_denoiseBackend.store(static_cast<int>(DenoiseBackendKind::Rnnoise),
        std::memory_order_release);
    g_denoiseResetEpoch.fetch_add(1, std::memory_order_acq_rel);
    if (!processOne(pipeline, block++, 0) ||
        g_denoiseEffectiveBackend.load(std::memory_order_acquire) !=
            static_cast<int>(DenoiseBackendKind::Rnnoise)) {
        std::printf("ERROR: switch to RNNoise was not applied at a block boundary\n");
        return 1;
    }

    g_denoiseBackend.store(static_cast<int>(DenoiseBackendKind::Dpdfnet),
        std::memory_order_release);
    g_denoiseResetEpoch.fetch_add(1, std::memory_order_acq_rel);
    if (!runDpdfnetEpoch(pipeline, block, 70, 10, 55)) {
        std::printf("ERROR: DPDFNet did not recover after switching back\n");
        return 1;
    }

    // Repeated resets exercise the epoch hand-off without allowing an old
    // worker FIFO to mute the first block of the next stream.
    for (int reset = 0; reset < 5; ++reset) {
        g_denoiseResetEpoch.fetch_add(1, std::memory_order_acq_rel);
        if (!runDpdfnetEpoch(pipeline, block, 30, 10, 22)) {
            std::printf("ERROR: DPDFNet failed after repeated epoch reset #%d\n",
                reset + 1);
            return 1;
        }
    }

    // Make the worker provably slower than the render cadence. The pipeline
    // must stop emitting unlimited silence and downgrade to RNNoise.
    pipeline.setDpdfnetWorkerDelayForTest(100);
    g_denoiseResetEpoch.fetch_add(1, std::memory_order_acq_rel);
    bool degraded = false;
    for (int i = 0; i < 12; ++i, ++block) {
        if (!processOne(pipeline, block, 0)) {
            std::printf("ERROR: watchdog test produced non-finite output\n");
            return 1;
        }
        if (g_denoiseEffectiveBackend.load(std::memory_order_acquire) ==
                static_cast<int>(DenoiseBackendKind::Rnnoise) &&
            g_dpdfnetDegraded.load(std::memory_order_acquire)) {
            degraded = true;
            break;
        }
    }
    if (!degraded) {
        std::printf("ERROR: DPDFNet underflow watchdog did not downgrade\n");
        return 1;
    }

    pipeline.setDpdfnetWorkerDelayForTest(0);
    Sleep(150);
    g_denoiseResetEpoch.fetch_add(1, std::memory_order_acq_rel);
    if (!runDpdfnetEpoch(pipeline, block, 70, 10, 55)) {
        std::printf("ERROR: DPDFNet did not recover after watchdog reset\n");
        return 1;
    }

    if (pipeline.dpdfnetInputDrops() != 0 || pipeline.dpdfnetOutputDrops() != 0) {
        std::printf("ERROR: DPDFNet FIFO drops detected (input=%llu output=%llu)\n",
            static_cast<unsigned long long>(pipeline.dpdfnetInputDrops()),
            static_cast<unsigned long long>(pipeline.dpdfnetOutputDrops()));
        return 1;
    }

    std::printf("DPDFNet pipeline switch smoke OK: resets=7 watchdog=1 underflows=%llu worker=%.1fus\n",
        static_cast<unsigned long long>(pipeline.dpdfnetUnderflows()),
        pipeline.dpdfnetWorkerProcUsEma());
    return 0;
}
