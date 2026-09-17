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

    // Upper bound on how long shutdown waits for the worker before giving up
    // and abandoning the session. The native Run() call cannot be interrupted
    // from the outside, so without a budget a stuck worker would hang the whole
    // application on a join() that never returns. Exposed so tests and callers
    // share one source of truth.
    static constexpr unsigned int WORKER_STOP_TIMEOUT_MS = 2000;

    // True once a worker had to be abandoned because it would not stop inside
    // the shutdown budget (it was stuck inside the native runtime). The session
    // is then leaked rather than freed, reports itself as not ready, and
    // prepare() refuses to reuse it - a restart is needed to get DPDFNet back.
    bool workerAbandoned() const;

    const std::string& prepareError() const;

    uint64_t inputDrops() const;
    uint64_t outputDrops() const;
    uint64_t outputUnderflows() const;
    double workerProcUsEma() const;

#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
    void setWorkerDelayForTest(unsigned int delayMs);
    void forceFailureForTest();
    void injectInvalidOutputForTest(int faultKind);
    bool validationTestFailedForTest() const;
    uint64_t epochShortCircuitsForTest() const;
#endif

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
