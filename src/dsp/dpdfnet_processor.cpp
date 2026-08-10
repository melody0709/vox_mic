#include "dpdfnet_processor.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <thread>
#include <vector>

#ifndef VOXMIC_ENABLE_DPDFNET
#define VOXMIC_ENABLE_DPDFNET 0
#endif

#if VOXMIC_ENABLE_DPDFNET
#include <sherpa-onnx/c-api/c-api.h>
#endif

namespace {

#if VOXMIC_ENABLE_DPDFNET
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

static std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
        value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
            value.c_str(), static_cast<int>(value.size()), result.data(), len,
            nullptr, nullptr) <= 0) {
        return {};
    }
    return result;
}

static bool isRegularFile(const std::wstring& path) {
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::wstring joinPath(const std::wstring& base, const wchar_t* leaf) {
    if (base.empty()) return leaf;
    if (base.back() == L'\\' || base.back() == L'/') return base + leaf;
    return base + L"\\" + leaf;
}

static bool makeAbsolutePath(std::wstring& path) {
    if (path.empty()) return false;

    DWORD capacity = MAX_PATH;
    for (;;) {
        std::vector<wchar_t> buffer(capacity);
        const DWORD length = GetFullPathNameW(path.c_str(), capacity,
            buffer.data(), nullptr);
        if (length == 0) return false;
        if (length < capacity - 1) {
            path.assign(buffer.data(), length);
            return true;
        }
        capacity = length + 1;
    }
}
#endif

template <size_t Capacity>
class TaggedBlockQueue {
public:
    struct Block {
        uint64_t epoch = 0;
        float samples[DPDFNET_BLOCK_SAMPLES]{};
    };

    bool push(uint64_t epoch, const float* samples) {
        const size_t write = m_write.load(std::memory_order_relaxed);
        const size_t next = (write + 1) % Capacity;
        if (next == m_read.load(std::memory_order_acquire)) return false;

        m_blocks[write].epoch = epoch;
        std::memcpy(m_blocks[write].samples, samples,
            sizeof(m_blocks[write].samples));
        m_write.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Block& block) {
        const size_t read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire)) return false;

        block = m_blocks[read];
        m_read.store((read + 1) % Capacity, std::memory_order_release);
        return true;
    }

    // Called only by the single consumer side of this queue.
    void discardAll() {
        m_read.store(m_write.load(std::memory_order_acquire),
            std::memory_order_release);
    }

private:
    std::array<Block, Capacity> m_blocks{};
    std::atomic<size_t> m_read{0};
    std::atomic<size_t> m_write{0};
};

} // namespace

struct DpdfnetProcessor::Impl {
    std::string prepareError;
#if VOXMIC_ENABLE_DPDFNET
    static constexpr size_t QUEUE_CAPACITY = 32;
    static constexpr size_t FIFO_CAPACITY = DPDFNET_BLOCK_SAMPLES * 64;
    static constexpr int MAX_OUTPUT_SAMPLES = DPDFNET_BLOCK_SAMPLES * 4;

    SherpaOnnxApi api;
    const SherpaOnnxOnlineSpeechDenoiser* denoiser = nullptr;
    std::thread worker;
    HANDLE wakeEvent = nullptr;
    HANDLE readyEvent = nullptr;
    std::atomic<bool> stop{false};
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
    std::atomic<uint64_t> requestedEpoch{1};
    uint64_t workerEpoch = 1;

    TaggedBlockQueue<QUEUE_CAPACITY> inputQueue;
    TaggedBlockQueue<QUEUE_CAPACITY> outputQueue;
    std::array<float, FIFO_CAPACITY> outputFifo{};
    size_t outputFifoCount = 0;

    int expectedSampleRate = 48000;
    std::atomic<uint64_t> inputDrops{0};
    std::atomic<uint64_t> outputDrops{0};
    std::atomic<uint64_t> outputUnderflows{0};
    std::atomic<double> workerProcUsEma{0.0};
    LARGE_INTEGER qpcFrequency{};
#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
    std::atomic<unsigned int> testWorkerDelayMs{0};
    std::atomic<bool> testForceFailure{false};
    std::atomic<int> testOutputFault{0};
    std::atomic<bool> testValidationFailed{false};
    std::atomic<uint64_t> testEpochShortCircuits{0};
#endif

    void signalWorker() {
        if (wakeEvent) SetEvent(wakeEvent);
    }

    void failWorker() {
        failed.store(true, std::memory_order_release);
        ready.store(false, std::memory_order_release);
        // If failure is reported on the worker itself, this auto-reset event
        // signal may be consumed by the next failed-state wait. That extra
        // wake is intentional; the following wait blocks until stopWorker().
        signalWorker();
    }

    bool validAudio(const SherpaOnnxDenoisedAudio* audio) const {
        if (!audio || audio->sample_rate != expectedSampleRate ||
            audio->n < 0 || audio->n > MAX_OUTPUT_SAMPLES ||
            (audio->n > 0 && audio->samples == nullptr)) {
            return false;
        }
        for (int32_t i = 0; i < audio->n; ++i) {
            if (!std::isfinite(audio->samples[i])) return false;
        }
        return true;
    }

    void resetWorkerState(uint64_t epoch) {
        if (api.reset && denoiser) api.reset(denoiser);
        workerEpoch = epoch;
        outputFifoCount = 0;
        // Do not clear inputQueue here. The render thread may already have
        // submitted the first block of the new epoch before the worker saw
        // requestedEpoch. Stale blocks are discarded after pop by their tag;
        // retaining the queue preserves the new epoch's first block.
    }

    void appendOutput(const float* samples, int32_t count, uint64_t epoch) {
        if (!samples || count <= 0) return;
        size_t offset = 0;
        while (offset < static_cast<size_t>(count)) {
            const size_t freeSpace = FIFO_CAPACITY - outputFifoCount;
            if (freeSpace == 0) {
                outputDrops.fetch_add(1, std::memory_order_relaxed);
                outputFifoCount = 0;
                break;
            }
            const size_t copyCount = (std::min)(freeSpace,
                static_cast<size_t>(count) - offset);
            std::memcpy(outputFifo.data() + outputFifoCount,
                samples + offset, copyCount * sizeof(float));
            outputFifoCount += copyCount;
            offset += copyCount;
        }

        while (outputFifoCount >= DPDFNET_BLOCK_SAMPLES) {
            if (!outputQueue.push(epoch, outputFifo.data())) {
                outputDrops.fetch_add(1, std::memory_order_relaxed);
            }
            outputFifoCount -= DPDFNET_BLOCK_SAMPLES;
            if (outputFifoCount > 0) {
                std::memmove(outputFifo.data(),
                    outputFifo.data() + DPDFNET_BLOCK_SAMPLES,
                    outputFifoCount * sizeof(float));
            }
        }
    }

    void workerLoop() {
        float warmup[DPDFNET_BLOCK_SAMPLES]{};
        const SherpaOnnxDenoisedAudio* warmupAudio = api.run(
            denoiser, warmup, DPDFNET_BLOCK_SAMPLES, expectedSampleRate);
        if (warmupAudio) {
            if (!validAudio(warmupAudio)) {
                api.destroyAudio(warmupAudio);
                failWorker();
            } else {
                api.destroyAudio(warmupAudio);
            }
        }
        if (!failed.load(std::memory_order_acquire)) {
            api.reset(denoiser);
            workerEpoch = requestedEpoch.load(std::memory_order_acquire);
        }
        ready.store(!failed.load(std::memory_order_acquire),
            std::memory_order_release);
        if (readyEvent) SetEvent(readyEvent);

        while (!stop.load(std::memory_order_acquire)) {
            if (failed.load(std::memory_order_acquire)) {
                if (wakeEvent) WaitForSingleObject(wakeEvent, INFINITE);
                continue;
            }

#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
            if (testForceFailure.exchange(false, std::memory_order_acq_rel)) {
                failWorker();
                continue;
            }
#endif

            const uint64_t requested = requestedEpoch.load(std::memory_order_acquire);
            if (requested != workerEpoch) {
                resetWorkerState(requested);
            }

            TaggedBlockQueue<QUEUE_CAPACITY>::Block input;
            if (!inputQueue.pop(input)) {
                if (wakeEvent) WaitForSingleObject(wakeEvent, 20);
                continue;
            }

            if (failed.load(std::memory_order_acquire)) {
                continue;
            }

            if (input.epoch < workerEpoch) continue;
            if (input.epoch > workerEpoch) {
                const uint64_t requestedAfterPop =
                    requestedEpoch.load(std::memory_order_acquire);
                if (requestedAfterPop > input.epoch) continue;
                resetWorkerState(input.epoch);
            }
            if (input.epoch != workerEpoch ||
                requestedEpoch.load(std::memory_order_acquire) != workerEpoch) {
                continue;
            }

#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
            const unsigned int testDelay =
                testWorkerDelayMs.load(std::memory_order_relaxed);
            if (testDelay > 0) Sleep(testDelay);
#endif
            LARGE_INTEGER t1{};
            LARGE_INTEGER t2{};
            QueryPerformanceCounter(&t1);
            const SherpaOnnxDenoisedAudio* audio = api.run(
                denoiser, input.samples, DPDFNET_BLOCK_SAMPLES,
                expectedSampleRate);
            QueryPerformanceCounter(&t2);
            if (qpcFrequency.QuadPart > 0) {
                const double procUs = static_cast<double>(t2.QuadPart - t1.QuadPart) *
                    1e6 / static_cast<double>(qpcFrequency.QuadPart);
                const double oldEma = workerProcUsEma.load(std::memory_order_relaxed);
                workerProcUsEma.store(oldEma * 0.95 + procUs * 0.05,
                    std::memory_order_relaxed);
            }

#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
            const int testFault = testOutputFault.exchange(0,
                std::memory_order_acq_rel);
            if (testFault != 0) {
                if (audio) api.destroyAudio(audio);
                float finiteSample = 0.0f;
                float nonFiniteSample = std::numeric_limits<float>::quiet_NaN();
                SherpaOnnxDenoisedAudio synthetic{
                    &finiteSample, 1, expectedSampleRate};
                if (testFault == 1) {
                    synthetic.sample_rate = expectedSampleRate + 1;
                } else if (testFault == 2) {
                    synthetic.n = MAX_OUTPUT_SAMPLES + 1;
                } else if (testFault == 3) {
                    synthetic.samples = nullptr;
                } else if (testFault == 4) {
                    synthetic.samples = &nonFiniteSample;
                }
                if (validAudio(&synthetic)) {
                    testValidationFailed.store(true, std::memory_order_release);
                }
                failWorker();
                continue;
            }
#endif

            if (!audio) continue;
            if (!validAudio(audio)) {
                api.destroyAudio(audio);
                failWorker();
                continue;
            }
            // A reset can arrive while Run() is executing. Never enqueue that
            // result after the render thread has cleared the old epoch;
            // otherwise stale output can briefly occupy the output FIFO.
            if (requestedEpoch.load(std::memory_order_acquire) != workerEpoch) {
                api.destroyAudio(audio);
                continue;
            }
            appendOutput(audio->samples, audio->n, workerEpoch);
            api.destroyAudio(audio);
        }
    }

    void stopWorker() {
        stop.store(true, std::memory_order_release);
        signalWorker();
        if (worker.joinable()) worker.join();
        if (readyEvent) {
            CloseHandle(readyEvent);
            readyEvent = nullptr;
        }
        if (wakeEvent) {
            CloseHandle(wakeEvent);
            wakeEvent = nullptr;
        }
        if (denoiser && api.destroy) {
            api.destroy(denoiser);
            denoiser = nullptr;
        }
        ready.store(false, std::memory_order_release);
        api.unload();
    }
#endif
};

DpdfnetProcessor::DpdfnetProcessor()
    : m_impl(std::make_unique<Impl>()) {
}

DpdfnetProcessor::~DpdfnetProcessor() {
#if VOXMIC_ENABLE_DPDFNET
    m_impl->stopWorker();
#endif
}

bool DpdfnetProcessor::prepare(const std::wstring& runtimeDirectory,
    const std::wstring& modelPath, int expectedSampleRate,
    int expectedFrameShift, std::string* errorMessage) {
#if !VOXMIC_ENABLE_DPDFNET
    (void)runtimeDirectory;
    (void)modelPath;
    (void)expectedSampleRate;
    (void)expectedFrameShift;
    const std::string error = "DPDFNet support is disabled in this build";
    if (errorMessage) *errorMessage = error;
    return false;
#else
    m_impl->stopWorker();
    m_impl->prepareError.clear();
    m_impl->failed.store(false, std::memory_order_release);
    m_impl->ready.store(false, std::memory_order_release);
    m_impl->expectedSampleRate = expectedSampleRate;
    m_impl->inputQueue.discardAll();
    m_impl->outputQueue.discardAll();
    m_impl->outputFifoCount = 0;
    m_impl->inputDrops.store(0, std::memory_order_relaxed);
    m_impl->outputDrops.store(0, std::memory_order_relaxed);
    m_impl->outputUnderflows.store(0, std::memory_order_relaxed);
    m_impl->workerProcUsEma.store(0.0, std::memory_order_relaxed);
#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
    m_impl->testEpochShortCircuits.store(0, std::memory_order_relaxed);
#endif
    QueryPerformanceFrequency(&m_impl->qpcFrequency);

    std::wstring absoluteRuntimeDirectory = runtimeDirectory;
    std::wstring absoluteModelPath = modelPath;
    if (!makeAbsolutePath(absoluteRuntimeDirectory) ||
        !makeAbsolutePath(absoluteModelPath)) {
        m_impl->prepareError = "Could not resolve DPDFNet runtime/model path";
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    const std::wstring dllPath = joinPath(absoluteRuntimeDirectory,
        L"sherpa-onnx-c-api.dll");
    if (!isRegularFile(dllPath)) {
        m_impl->prepareError = "Missing sherpa-onnx-c-api.dll";
    } else if (!isRegularFile(joinPath(absoluteRuntimeDirectory,
            L"onnxruntime.dll"))) {
        m_impl->prepareError = "Missing onnxruntime.dll";
    } else if (!isRegularFile(joinPath(absoluteRuntimeDirectory,
            L"onnxruntime_providers_shared.dll"))) {
        m_impl->prepareError = "Missing onnxruntime_providers_shared.dll";
    } else if (!isRegularFile(absoluteModelPath)) {
        m_impl->prepareError = "Missing DPDFNet model";
    }
    if (!m_impl->prepareError.empty()) {
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    m_impl->api.module = LoadLibraryExW(dllPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!m_impl->api.module) {
        char buf[128];
        snprintf(buf, sizeof(buf), "LoadLibraryExW failed (%lu)",
            static_cast<unsigned long>(GetLastError()));
        m_impl->prepareError = buf;
    }

    auto resolve = [&](const char* name, FARPROC* destination) -> bool {
        if (!m_impl->api.module) return false;
        FARPROC address = GetProcAddress(m_impl->api.module, name);
        if (!address) {
            m_impl->prepareError = std::string("Missing sherpa-onnx API symbol: ") + name;
            return false;
        }
        *destination = address;
        return true;
    };

    if (m_impl->prepareError.empty()) {
        if (!resolve("SherpaOnnxCreateOnlineSpeechDenoiser",
                reinterpret_cast<FARPROC*>(&m_impl->api.create)) ||
            !resolve("SherpaOnnxDestroyOnlineSpeechDenoiser",
                reinterpret_cast<FARPROC*>(&m_impl->api.destroy)) ||
            !resolve("SherpaOnnxOnlineSpeechDenoiserGetSampleRate",
                reinterpret_cast<FARPROC*>(&m_impl->api.getSampleRate)) ||
            !resolve("SherpaOnnxOnlineSpeechDenoiserGetFrameShiftInSamples",
                reinterpret_cast<FARPROC*>(&m_impl->api.getFrameShift)) ||
            !resolve("SherpaOnnxOnlineSpeechDenoiserRun",
                reinterpret_cast<FARPROC*>(&m_impl->api.run)) ||
            !resolve("SherpaOnnxOnlineSpeechDenoiserReset",
                reinterpret_cast<FARPROC*>(&m_impl->api.reset)) ||
            !resolve("SherpaOnnxDestroyDenoisedAudio",
                reinterpret_cast<FARPROC*>(&m_impl->api.destroyAudio))) {
            // Keep the detailed symbol error above.
        }
    }

    if (!m_impl->prepareError.empty()) {
        m_impl->api.unload();
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    const std::string modelUtf8 = wideToUtf8(absoluteModelPath);
    if (modelUtf8.empty()) {
        m_impl->prepareError = "Could not convert DPDFNet model path to UTF-8";
        m_impl->api.unload();
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    SherpaOnnxOnlineSpeechDenoiserConfig config{};
    config.model.num_threads = 1;
    config.model.debug = 0;
    config.model.provider = "cpu";
    config.model.dpdfnet.model = modelUtf8.c_str();
    m_impl->denoiser = m_impl->api.create(&config);
    if (!m_impl->denoiser) {
        m_impl->prepareError = "sherpa-onnx failed to create the DPDFNet session";
        m_impl->api.unload();
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    const int sampleRate = m_impl->api.getSampleRate(m_impl->denoiser);
    const int frameShift = m_impl->api.getFrameShift(m_impl->denoiser);
    if (sampleRate != expectedSampleRate || frameShift != expectedFrameShift) {
        char buf[192];
        snprintf(buf, sizeof(buf),
            "DPDFNet contract mismatch (sample rate=%d, frame shift=%d)",
            sampleRate, frameShift);
        m_impl->prepareError = buf;
        m_impl->stopWorker();
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    m_impl->wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_impl->readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_impl->wakeEvent || !m_impl->readyEvent) {
        m_impl->prepareError = "Could not create DPDFNet worker events";
        m_impl->stopWorker();
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    m_impl->stop.store(false, std::memory_order_release);
    m_impl->requestedEpoch.store(1, std::memory_order_release);
    m_impl->workerEpoch = 1;
    m_impl->worker = std::thread([impl = m_impl.get()] { impl->workerLoop(); });
    const DWORD waitResult = WaitForSingleObject(m_impl->readyEvent, 10000);
    if (waitResult != WAIT_OBJECT_0 || m_impl->failed.load(std::memory_order_acquire)) {
        m_impl->prepareError = (waitResult == WAIT_TIMEOUT)
            ? "DPDFNet worker warm-up timed out"
            : "DPDFNet worker warm-up failed";
        m_impl->stopWorker();
        if (errorMessage) *errorMessage = m_impl->prepareError;
        return false;
    }

    m_impl->ready.store(true, std::memory_order_release);
    if (errorMessage) errorMessage->clear();
    return true;
#endif
}

void DpdfnetProcessor::setEpoch(uint64_t epoch) {
#if VOXMIC_ENABLE_DPDFNET
    if (!m_impl->ready.load(std::memory_order_acquire) ||
        m_impl->failed.load(std::memory_order_acquire)) {
#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
        m_impl->testEpochShortCircuits.fetch_add(1,
            std::memory_order_relaxed);
#endif
        return;
    }
    m_impl->requestedEpoch.store(epoch, std::memory_order_release);
    // The output queue is consumed by the render thread, so it is safe for
    // this side to discard old output immediately.  The worker tags every new
    // result with its own epoch, providing a second stale-data guard.
    m_impl->outputQueue.discardAll();
    m_impl->signalWorker();
#else
    (void)epoch;
#endif
}

bool DpdfnetProcessor::processBlock(const float* input, float* output,
    uint64_t epoch) {
#if !VOXMIC_ENABLE_DPDFNET
    (void)input;
    (void)epoch;
    if (output) std::memset(output, 0, DPDFNET_BLOCK_SAMPLES * sizeof(float));
    return false;
#else
    if (!output) return false;
    std::memset(output, 0, DPDFNET_BLOCK_SAMPLES * sizeof(float));
    if (!input) return false;
    if (!m_impl->ready.load(std::memory_order_acquire) ||
        m_impl->failed.load(std::memory_order_acquire)) {
        return false;
    }

    m_impl->requestedEpoch.store(epoch, std::memory_order_release);
    if (!m_impl->inputQueue.push(epoch, input)) {
        m_impl->inputDrops.fetch_add(1, std::memory_order_relaxed);
    }
    m_impl->signalWorker();

    TaggedBlockQueue<Impl::QUEUE_CAPACITY>::Block block;
    while (m_impl->outputQueue.pop(block)) {
        if (block.epoch == epoch) {
            std::memcpy(output, block.samples,
                DPDFNET_BLOCK_SAMPLES * sizeof(float));
            return true;
        }
        // Discard output belonging to an older stream epoch.  A future epoch
        // cannot be produced before this render call, so it is also stale.
    }

    m_impl->outputUnderflows.fetch_add(1, std::memory_order_relaxed);
    return false;
#endif
}

bool DpdfnetProcessor::isReady() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->ready.load(std::memory_order_acquire) &&
        !m_impl->failed.load(std::memory_order_acquire);
#else
    return false;
#endif
}

bool DpdfnetProcessor::hasFailed() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->failed.load(std::memory_order_acquire);
#else
    return false;
#endif
}

const std::string& DpdfnetProcessor::prepareError() const {
    return m_impl->prepareError;
}

uint64_t DpdfnetProcessor::inputDrops() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->inputDrops.load(std::memory_order_relaxed);
#else
    return 0;
#endif
}

uint64_t DpdfnetProcessor::outputDrops() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->outputDrops.load(std::memory_order_relaxed);
#else
    return 0;
#endif
}

uint64_t DpdfnetProcessor::outputUnderflows() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->outputUnderflows.load(std::memory_order_relaxed);
#else
    return 0;
#endif
}

double DpdfnetProcessor::workerProcUsEma() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->workerProcUsEma.load(std::memory_order_relaxed);
#else
    return 0.0;
#endif
}

#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
void DpdfnetProcessor::setWorkerDelayForTest(unsigned int delayMs) {
#if VOXMIC_ENABLE_DPDFNET
    m_impl->testWorkerDelayMs.store(delayMs, std::memory_order_relaxed);
#else
    (void)delayMs;
#endif
}

void DpdfnetProcessor::forceFailureForTest() {
#if VOXMIC_ENABLE_DPDFNET
    m_impl->testForceFailure.store(true, std::memory_order_release);
    m_impl->signalWorker();
#endif
}

void DpdfnetProcessor::injectInvalidOutputForTest(int faultKind) {
#if VOXMIC_ENABLE_DPDFNET
    m_impl->testOutputFault.store(faultKind, std::memory_order_release);
    m_impl->signalWorker();
#else
    (void)faultKind;
#endif
}

bool DpdfnetProcessor::validationTestFailedForTest() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->testValidationFailed.load(std::memory_order_acquire);
#else
    return false;
#endif
}

uint64_t DpdfnetProcessor::epochShortCircuitsForTest() const {
#if VOXMIC_ENABLE_DPDFNET
    return m_impl->testEpochShortCircuits.load(std::memory_order_relaxed);
#else
    return 0;
#endif
}
#endif
