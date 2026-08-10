#include "dsp/dpdfnet_processor.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static std::wstring utf8ToWide(const char* value) {
    if (!value || !*value) return {};
    int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        value, -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring result(static_cast<size_t>(len), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value, -1, result.data(), len) <= 0) {
        return {};
    }
    result.resize(static_cast<size_t>(len - 1));
    return result;
}

static double qpcUs(LARGE_INTEGER begin, LARGE_INTEGER end,
    LARGE_INTEGER frequency) {
    return static_cast<double>(end.QuadPart - begin.QuadPart) * 1e6 /
        static_cast<double>(frequency.QuadPart);
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Usage: dpdfnet_smoke <runtime-directory> <model-path>\n");
        return 2;
    }

    std::wstring runtimeDirectory = utf8ToWide(argv[1]);
    std::wstring modelPath = utf8ToWide(argv[2]);
    if (runtimeDirectory.empty() || modelPath.empty()) {
        std::printf("ERROR: path conversion failed\n");
        return 2;
    }

    DpdfnetProcessor processor;
    std::string error;
    if (!processor.prepare(runtimeDirectory, modelPath, 48000, 480, &error)) {
        std::printf("ERROR: DPDFNet prepare failed: %s\n", error.c_str());
        return 1;
    }

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    processor.setEpoch(1);

    int outputBlocks = 0;
    double maxSubmitUs = 0.0;
    for (int block = 0; block < 120; ++block) {
        float input[DPDFNET_BLOCK_SAMPLES];
        float output[DPDFNET_BLOCK_SAMPLES];
        for (int i = 0; i < DPDFNET_BLOCK_SAMPLES; ++i) {
            const double t = (block * DPDFNET_BLOCK_SAMPLES + i) / 48000.0;
            input[i] = static_cast<float>(
                0.08 * std::sin(2.0 * 3.141592653589793 * 220.0 * t) +
                0.01 * std::sin(2.0 * 3.141592653589793 * 3100.0 * t));
        }

        LARGE_INTEGER begin{}, end{};
        QueryPerformanceCounter(&begin);
        const bool hasOutput = processor.processBlock(input, output, 1);
        QueryPerformanceCounter(&end);
        maxSubmitUs = (std::max)(maxSubmitUs, qpcUs(begin, end, frequency));
        if (hasOutput) ++outputBlocks;
        for (float sample : output) {
            if (!std::isfinite(sample)) {
                std::printf("ERROR: non-finite output before reset\n");
                return 1;
            }
        }
        Sleep(10);
    }

    const int beforeResetBlocks = outputBlocks;
    processor.setEpoch(2);
    int afterResetBlocks = 0;
    for (int block = 0; block < 40; ++block) {
        float input[DPDFNET_BLOCK_SAMPLES]{};
        float output[DPDFNET_BLOCK_SAMPLES];
        if (processor.processBlock(input, output, 2)) ++afterResetBlocks;
        for (float sample : output) {
            if (!std::isfinite(sample)) {
                std::printf("ERROR: non-finite output after reset\n");
                return 1;
            }
        }
        Sleep(10);
    }

    int repeatedResetMinimum = 1000;
    for (uint64_t epoch = 3; epoch <= 6; ++epoch) {
        processor.setEpoch(epoch);
        int epochOutputs = 0;
        for (int block = 0; block < 30; ++block) {
            float input[DPDFNET_BLOCK_SAMPLES]{};
            float output[DPDFNET_BLOCK_SAMPLES];
            if (processor.processBlock(input, output, epoch)) ++epochOutputs;
            for (float sample : output) {
                if (!std::isfinite(sample)) {
                    std::printf("ERROR: non-finite output after repeated reset\n");
                    return 1;
                }
            }
            Sleep(10);
        }
        repeatedResetMinimum = (std::min)(repeatedResetMinimum, epochOutputs);
    }

    std::printf("DPDFNet smoke OK: output=%d/%d after_reset=%d repeated_reset_min=%d "
        "max_submit=%.1fus worker_ema=%.1fus input_drops=%llu output_drops=%llu underflows=%llu\n",
        beforeResetBlocks, 120, afterResetBlocks, repeatedResetMinimum,
        maxSubmitUs,
        processor.workerProcUsEma(),
        static_cast<unsigned long long>(processor.inputDrops()),
        static_cast<unsigned long long>(processor.outputDrops()),
        static_cast<unsigned long long>(processor.outputUnderflows()));

    if (beforeResetBlocks < 90 || afterResetBlocks < 20 ||
        repeatedResetMinimum < 15 || processor.hasFailed() ||
        processor.inputDrops() != 0) {
        std::printf("ERROR: DPDFNet streaming contract did not meet smoke-test thresholds\n");
        return 1;
    }
    return 0;
}
