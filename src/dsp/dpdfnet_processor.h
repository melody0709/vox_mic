#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

// DPDFNet's online API consumes 480 samples at 48 kHz for the model selected
// by VoxMic.  Keep this contract local so the adapter does not need to include
// sherpa-onnx headers or create an import-library dependency.
constexpr int DPDFNET_BLOCK_SAMPLES = 480;

class DpdfnetProcessor {
public:
    DpdfnetProcessor();
    ~DpdfnetProcessor();

    DpdfnetProcessor(const DpdfnetProcessor&) = delete;
    DpdfnetProcessor& operator=(const DpdfnetProcessor&) = delete;

    // Loads the optional runtime and creates the online session.  This must be
    // called before the WASAPI render thread starts.
    bool prepare(const std::wstring& runtimeDirectory,
                 const std::wstring& modelPath,
                 int expectedSampleRate,
                 int expectedFrameShift,
                 std::string* errorMessage = nullptr);

    // Changes the logical stream epoch without calling the stateful C API from
    // the render thread.  The worker observes the epoch and performs Reset().
    void setEpoch(uint64_t epoch);

    // Non-blocking render-thread operation.  The input is always submitted to
    // the worker.  Returns true only when a denoised 480-sample block for the
    // same epoch is available in the output FIFO; otherwise output is filled
    // with silence and the worker catches up without blocking WASAPI.
    bool processBlock(const float* input, float* output, uint64_t epoch);

    bool isReady() const;
    bool hasFailed() const;
    const std::string& prepareError() const;

    uint64_t inputDrops() const;
    uint64_t outputDrops() const;
    uint64_t outputUnderflows() const;
    double workerProcUsEma() const;

#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
    void setWorkerDelayForTest(unsigned int delayMs);
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
