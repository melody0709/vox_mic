#include "dsp/dpdfnet_processor.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <string>

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

static bool runInvalidOutputFault(const std::wstring& runtimeDirectory,
    const std::wstring& modelPath, int faultKind) {
    DpdfnetProcessor processor;
    std::string error;
    if (!processor.prepare(runtimeDirectory, modelPath, 48000, 480, &error)) {
        std::printf("ERROR: invalid-output prepare failed: %s\n", error.c_str());
        return false;
    }

    float input[DPDFNET_BLOCK_SAMPLES]{};
    float output[DPDFNET_BLOCK_SAMPLES]{};
    processor.processBlock(input, output, 1);
    processor.injectInvalidOutputForTest(faultKind);

    const ULONGLONG deadline = GetTickCount64() + 2000;
    while (GetTickCount64() < deadline && !processor.hasFailed()) {
        processor.processBlock(input, output, 1);
        Sleep(1);
    }
    return processor.hasFailed() && !processor.isReady() &&
        !processor.validationTestFailedForTest();
}

// A worker that cannot observe the stop request must not be able to block
// shutdown. setWorkerDelayForTest() parks the worker inside its own Sleep(),
// which has the same shape as being stuck inside the native Run() call: the
// stop request cannot be seen until that sleep ends. Before the shutdown budget
// existed this destructor would have waited out the full delay; it must now
// give up at the budget instead of hanging.
static bool runAbandonedWorkerCase(const std::wstring& runtimeDirectory,
    const std::wstring& modelPath, ULONGLONG* elapsedMs) {
    const ULONGLONG start = GetTickCount64();
    {
        DpdfnetProcessor processor;
        std::string error;
        if (!processor.prepare(runtimeDirectory, modelPath, 48000, 480, &error)) {
            std::printf("ERROR: abandon-case prepare failed: %s\n", error.c_str());
            return false;
        }

        float input[DPDFNET_BLOCK_SAMPLES]{};
        float output[DPDFNET_BLOCK_SAMPLES]{};
        processor.setWorkerDelayForTest(10000);
        processor.processBlock(input, output, 1);
        Sleep(300); // let the worker pick the block up and enter the delay
    }
    *elapsedMs = GetTickCount64() - start;

    const ULONGLONG budgetMs = DpdfnetProcessor::WORKER_STOP_TIMEOUT_MS;
    if (*elapsedMs + 200 < budgetMs) {
        std::printf("ERROR: abandoned worker did not wait out the budget "
            "(%llums < %llums)\n",
            static_cast<unsigned long long>(*elapsedMs),
            static_cast<unsigned long long>(budgetMs));
        return false;
    }
    if (*elapsedMs >= budgetMs + 2000) {
        std::printf("ERROR: shutdown was not bounded (%llums)\n",
            static_cast<unsigned long long>(*elapsedMs));
        return false;
    }
    return true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("Usage: dpdfnet_failure_smoke <runtime-directory> <model-path>\n");
        return 2;
    }

    const std::wstring runtimeDirectory = utf8ToWide(argv[1]);
    const std::wstring modelPath = utf8ToWide(argv[2]);
    if (runtimeDirectory.empty() || modelPath.empty()) {
        std::printf("ERROR: path conversion failed\n");
        return 2;
    }

    for (int faultKind = 1; faultKind <= 4; ++faultKind) {
        if (!runInvalidOutputFault(runtimeDirectory, modelPath, faultKind)) {
            std::printf("ERROR: invalid output fault %d was not rejected\n", faultKind);
            return 1;
        }
    }

    bool failed = false;
    bool readyAfterFailure = true;
    ULONGLONG destructorStart = 0;
    ULONGLONG destructorEnd = 0;

    {
        DpdfnetProcessor processor;
        std::string error;
        if (!processor.prepare(runtimeDirectory, modelPath, 48000, 480, &error)) {
            std::printf("ERROR: DPDFNet prepare failed: %s\n", error.c_str());
            return 1;
        }

        float input[DPDFNET_BLOCK_SAMPLES]{};
        float output[DPDFNET_BLOCK_SAMPLES]{};
        processor.processBlock(input, output, 1);
        processor.forceFailureForTest();

        const ULONGLONG deadline = GetTickCount64() + 2000;
        while (GetTickCount64() < deadline && !processor.hasFailed()) {
            Sleep(1);
        }
        failed = processor.hasFailed();
        readyAfterFailure = processor.isReady();
        if (!failed || readyAfterFailure) {
            std::printf("ERROR: failed worker state was not converged (failed=%d ready=%d)\n",
                failed ? 1 : 0, readyAfterFailure ? 1 : 0);
            return 1;
        }

        for (uint64_t epoch = 2; epoch < 12; ++epoch) {
            processor.setEpoch(epoch);
            processor.processBlock(input, output, epoch);
        }
        if (processor.epochShortCircuitsForTest() != 10) {
            std::printf("ERROR: setEpoch was not short-circuited after failure "
                "(count=%llu)\n",
                static_cast<unsigned long long>(
                    processor.epochShortCircuitsForTest()));
            return 1;
        }

        destructorStart = GetTickCount64();
    }
    destructorEnd = GetTickCount64();

    const ULONGLONG destructorMs = destructorEnd - destructorStart;

    ULONGLONG abandonedMs = 0;
    if (!runAbandonedWorkerCase(runtimeDirectory, modelPath, &abandonedMs)) {
        return 1;
    }

    std::printf("DPDFNet failure smoke OK: invalid_faults=4 failed=%d ready=%d "
        "destructor=%llums abandoned=%llums\n",
        failed ? 1 : 0, readyAfterFailure ? 1 : 0,
        static_cast<unsigned long long>(destructorMs),
        static_cast<unsigned long long>(abandonedMs));
    if (destructorMs >= 100) {
        std::printf("ERROR: failed worker destructor exceeded 100ms\n");
        return 1;
    }
    return 0;
}
