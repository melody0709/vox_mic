#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

extern "C" {
#include "rnnoise.h"
}

#include "dsp/biquad.h"
#include "dsp/dpdfnet_processor.h"

enum class DenoiseBackendKind : int {
    Rnnoise = 0,
    Dpdfnet = 1,
};

extern std::atomic<float> g_gain;
extern std::atomic<bool> g_eqEnabled;
extern std::atomic<float> g_eqPresence;
extern std::atomic<float> g_eqBassCut;
extern std::atomic<bool> g_compressorEnabled;
extern std::atomic<bool> g_nrEnabled;
extern std::atomic<float> g_nrStrength;
extern std::atomic<int> g_denoiseBackend;
extern std::atomic<uint64_t> g_denoiseResetEpoch;
extern std::atomic<bool> g_dpdfnetAvailable;
extern std::atomic<bool> g_dpdfnetDegraded;
extern std::atomic<int> g_denoiseEffectiveBackend;

class DspPipeline {
public:
    DspPipeline() = default;

    ~DspPipeline() {
        if (m_rnnoise) rnnoise_destroy(m_rnnoise);
    }

    DspPipeline(const DspPipeline&) = delete;
    DspPipeline& operator=(const DspPipeline&) = delete;

    bool init(float sampleRate, const std::wstring& runtimeDirectory,
        const std::wstring& modelPath) {
        m_sampleRate = sampleRate;
        if (!m_rnnoise) m_rnnoise = rnnoise_create(NULL);
        if (!m_rnnoise) {
            printf("ERROR: RNNoise initialization failed\n");
            return false;
        }

        std::string dpdfnetError;
        const bool dpdfnetReady = m_dpdfnet.prepare(runtimeDirectory, modelPath,
            static_cast<int>(sampleRate), 480, &dpdfnetError);
        g_dpdfnetAvailable.store(dpdfnetReady, std::memory_order_release);
        g_dpdfnetDegraded.store(false, std::memory_order_release);
        if (!dpdfnetReady) {
            if (dpdfnetError.empty()) dpdfnetError = m_dpdfnet.prepareError();
            if (dpdfnetError.empty()) dpdfnetError = "unknown error";
            printf("[DPDFNet] unavailable: %s\n", dpdfnetError.c_str());
        } else {
            printf("[DPDFNet] ready: 48 kHz / 480-sample online backend\n");
        }

        m_rnnoiseStrength = g_nrStrength.load(std::memory_order_relaxed);
        rnnoise_set_strength(m_rnnoise, m_rnnoiseStrength);
        m_nrActive = g_nrEnabled.load(std::memory_order_relaxed);
        m_eqActive = g_eqEnabled.load(std::memory_order_relaxed);
        m_lastPresence = g_eqPresence.load(std::memory_order_relaxed);
        m_lastBassCut = g_eqBassCut.load(std::memory_order_relaxed);
        m_compActive = g_compressorEnabled.load(std::memory_order_relaxed);
        configureEq();
        resetPostDenoiseState();

        m_lastRequestedBackend = -1;
        m_seenGlobalEpoch = 0;
        m_streamEpoch = 1;
        resetDpdfnetWatchdog();
        g_denoiseEffectiveBackend.store(
            static_cast<int>(DenoiseBackendKind::Rnnoise),
            std::memory_order_release);
        return true;
    }

    void process(float* samples, int numSamples, float sampleRate) {
        if (numSamples != 480) {
            processFallback(samples, numSamples, sampleRate);
            return;
        }

        const bool nrOn = g_nrEnabled.load(std::memory_order_relaxed);
        const int requestedBackend = g_denoiseBackend.load(std::memory_order_relaxed);
        const uint64_t globalEpoch = g_denoiseResetEpoch.load(std::memory_order_acquire);
        const float nrStrength = g_nrStrength.load(std::memory_order_relaxed);

        const bool settingsChanged =
            nrOn != m_nrActive ||
            requestedBackend != m_lastRequestedBackend ||
            globalEpoch != m_seenGlobalEpoch;
        if (settingsChanged) {
            resetStreamState();
            m_nrActive = nrOn;
            m_lastRequestedBackend = requestedBackend;
            m_seenGlobalEpoch = globalEpoch;
            m_activeBackend = chooseEffectiveBackend(requestedBackend);
            g_denoiseEffectiveBackend.store(static_cast<int>(m_activeBackend),
                std::memory_order_release);
        }

        if (std::fabs(nrStrength - m_rnnoiseStrength) > 0.0001f) {
            m_rnnoiseStrength = nrStrength;
            if (m_rnnoise) rnnoise_set_strength(m_rnnoise, nrStrength);
        }

        const bool eqOn = g_eqEnabled.load(std::memory_order_relaxed);
        const bool compOn = g_compressorEnabled.load(std::memory_order_relaxed);
        const float presence = g_eqPresence.load(std::memory_order_relaxed);
        const float bassCut = g_eqBassCut.load(std::memory_order_relaxed);
        if (eqOn != m_eqActive || presence != m_lastPresence ||
            bassCut != m_lastBassCut || compOn != m_compActive) {
            updateSettings(sampleRate);
        }

        if (!nrOn) {
            processPostDenoise(samples, numSamples);
            return;
        }

        if (m_activeBackend == DenoiseBackendKind::Dpdfnet) {
            float denoised[480]{};
            if (!m_dpdfnet.isReady()) {
                degradeDpdfnet(m_dpdfnet.hasFailed()
                    ? "worker failed"
                    : "runtime became unavailable");
                processRnnoise(samples);
            } else {
                const bool haveOutput = m_dpdfnet.processBlock(
                    samples, denoised, m_streamEpoch);
                if (m_dpdfnet.hasFailed()) {
                    degradeDpdfnet("worker failed");
                    processRnnoise(samples);
                } else if (haveOutput) {
                    m_dpdfnetSeenOutput = true;
                    m_consecutiveDpdfnetUnderflows = 0;
                    std::memcpy(samples, denoised, sizeof(denoised));
                } else if (recordDpdfnetUnderflow()) {
                    char detail[112];
                    snprintf(detail, sizeof(detail),
                        "worker produced no output for %u consecutive 10 ms blocks",
                        m_consecutiveDpdfnetUnderflows);
                    degradeDpdfnet(detail);
                    processRnnoise(samples);
                } else {
                    // The online model needs STFT context after an epoch
                    // reset. A bounded warm-up silence keeps its delayed
                    // stream aligned without allowing a stuck worker to mute
                    // the microphone indefinitely.
                    std::memset(samples, 0, sizeof(denoised));
                }
            }
        } else {
            processRnnoise(samples);
        }

        processPostDenoise(samples, numSamples);
    }

    bool dpdfnetReady() const { return m_dpdfnet.isReady(); }
    uint64_t dpdfnetInputDrops() const { return m_dpdfnet.inputDrops(); }
    uint64_t dpdfnetOutputDrops() const { return m_dpdfnet.outputDrops(); }
    uint64_t dpdfnetUnderflows() const { return m_dpdfnet.outputUnderflows(); }
    double dpdfnetWorkerProcUsEma() const { return m_dpdfnet.workerProcUsEma(); }

#if defined(VOXMIC_DPDFNET_TEST_HOOKS)
    void setDpdfnetWorkerDelayForTest(unsigned int delayMs) {
        m_dpdfnet.setWorkerDelayForTest(delayMs);
    }
#endif

private:
    static constexpr unsigned int DPDFNET_WARMUP_UNDERFLOW_BLOCKS = 4;
    static constexpr unsigned int DPDFNET_STEADY_UNDERFLOW_BLOCKS = 3;

    DenoiseBackendKind chooseEffectiveBackend(int requested) const {
        if (requested == static_cast<int>(DenoiseBackendKind::Dpdfnet) &&
            m_dpdfnet.isReady() && !m_dpdfnetDegraded) {
            return DenoiseBackendKind::Dpdfnet;
        }
        return DenoiseBackendKind::Rnnoise;
    }

    void processRnnoise(float* samples) {
        if (m_rnnoise) rnnoise_process_frame(m_rnnoise, samples, samples);
    }

    bool recordDpdfnetUnderflow() {
        ++m_consecutiveDpdfnetUnderflows;
        if (m_dpdfnetSeenOutput) {
            return m_consecutiveDpdfnetUnderflows >=
                DPDFNET_STEADY_UNDERFLOW_BLOCKS;
        }
        return m_consecutiveDpdfnetUnderflows >
            DPDFNET_WARMUP_UNDERFLOW_BLOCKS;
    }

    void resetDpdfnetWatchdog() {
        m_dpdfnetSeenOutput = false;
        m_consecutiveDpdfnetUnderflows = 0;
        m_dpdfnetDegraded = false;
        g_dpdfnetDegraded.store(false, std::memory_order_release);
    }

    void degradeDpdfnet(const char* reason) {
        const bool processorReady = m_dpdfnet.isReady();
        if (!m_dpdfnetDegraded) {
            printf("[DPDFNet] %s; falling back to RNNoise\n", reason);
            m_dpdfnetDegraded = true;
            // Make pending delayed output stale before returning to RNNoise.
            ++m_streamEpoch;
            m_dpdfnet.setEpoch(m_streamEpoch);
        }
        if (!processorReady) {
            g_dpdfnetAvailable.store(false, std::memory_order_release);
            // A failed session cannot be recreated on the render thread;
            // reserve degraded for a ready-but-stalled worker, and require a
            // fresh prepare (normally an application restart) for this case.
            g_dpdfnetDegraded.store(false, std::memory_order_release);
        } else {
            g_dpdfnetDegraded.store(true, std::memory_order_release);
        }
        m_activeBackend = DenoiseBackendKind::Rnnoise;
        g_denoiseEffectiveBackend.store(
            static_cast<int>(m_activeBackend), std::memory_order_release);
    }

    void configureEq() {
        if (m_eqActive) {
            m_bq[0].setHPF(80.0f, 0.707f, m_sampleRate);
            m_bq[1].setLowShelf(120.0f, m_lastBassCut, 0.7f, m_sampleRate);
            m_bq[2].setPeak(250.0f, m_lastBassCut * 0.5f, 0.6f, m_sampleRate);
            m_bq[3].setPeak(2500.0f, m_lastPresence, 0.8f, m_sampleRate);
            m_bq[4].setPeak(3200.0f, m_lastPresence * 0.6f, 0.7f, m_sampleRate);
            m_bq[5].setHighShelf(8000.0f, m_lastPresence * 0.25f, 0.7f, m_sampleRate);
        }
        for (auto& bq : m_bq) bq.reset();
    }

    void updateSettings(float sampleRate) {
        m_sampleRate = sampleRate;
        m_eqActive = g_eqEnabled.load(std::memory_order_relaxed);
        m_lastPresence = g_eqPresence.load(std::memory_order_relaxed);
        m_lastBassCut = g_eqBassCut.load(std::memory_order_relaxed);
        m_compActive = g_compressorEnabled.load(std::memory_order_relaxed);
        configureEq();
        if (m_compActive != m_lastConfiguredCompActive) {
            m_comp.rmsState = 0.0f;
            m_comp.envDb = 0.0f;
            m_lastConfiguredCompActive = m_compActive;
        }
    }

    void resetPostDenoiseState() {
        for (auto& bq : m_bq) bq.reset();
        m_comp.rmsState = 0.0f;
        m_comp.envDb = 0.0f;
        m_limiterGain = 1.0f;
    }

    void resetStreamState() {
        if (m_rnnoise) rnnoise_reset(m_rnnoise);
        ++m_streamEpoch;
        m_dpdfnet.setEpoch(m_streamEpoch);
        resetDpdfnetWatchdog();
        resetPostDenoiseState();
    }

    void processFallback(float* samples, int numSamples, float sampleRate) {
        (void)sampleRate;
        processPostDenoise(samples, numSamples);
    }

    void processPostDenoise(float* samples, int numSamples) {
        const bool eqOn = g_eqEnabled.load(std::memory_order_relaxed);
        const bool compOn = g_compressorEnabled.load(std::memory_order_relaxed);
        if (eqOn) {
            for (int i = 0; i < numSamples; i++) {
                float x = samples[i];
                for (auto& bq : m_bq) x = bq.process(x);
                samples[i] = x;
            }
        }
        if (compOn) processCompressor(samples, numSamples);
        processLimiter(samples, numSamples);
    }

    struct CompState {
        float rmsState = 0.0f;
        float envDb = 0.0f;
    };

    DenoiseState* m_rnnoise = nullptr;
    DpdfnetProcessor m_dpdfnet;
    float m_sampleRate = 48000.0f;
    float m_rnnoiseStrength = 0.6f;
    bool m_nrActive = true;
    bool m_eqActive = true;
    float m_lastPresence = 3.0f;
    float m_lastBassCut = -3.0f;
    bool m_compActive = true;
    bool m_lastConfiguredCompActive = true;
    int m_lastRequestedBackend = -1;
    uint64_t m_seenGlobalEpoch = 0;
    uint64_t m_streamEpoch = 1;
    DenoiseBackendKind m_activeBackend = DenoiseBackendKind::Rnnoise;
    bool m_dpdfnetSeenOutput = false;
    unsigned int m_consecutiveDpdfnetUnderflows = 0;
    bool m_dpdfnetDegraded = false;
    Biquad m_bq[6];
    CompState m_comp;
    float m_limiterGain = 1.0f;

    void processCompressor(float* samples, int numSamples) {
        const float thresholdDb = -18.0f;
        const float ratio = 3.0f;
        const float kneeDb = 6.0f;
        const float attackMs = 5.0f;
        const float releaseMs = 50.0f;
        const float rmsMs = 10.0f;
        const float makeupDb = 5.0f;
        const float rmsCoeff = expf(-1.0f /
            (0.001f * rmsMs * 48000.0f / numSamples));
        const float attCoeff = expf(-1.0f /
            (0.001f * attackMs * 48000.0f));
        const float relCoeff = expf(-1.0f /
            (0.001f * releaseMs * 48000.0f));
        const float kneeHalf = kneeDb * 0.5f;
        const float slope = 1.0f - 1.0f / ratio;
        const float makeupGain = powf(10.0f, makeupDb / 20.0f);

        for (int i = 0; i < numSamples; i++) {
            float x = samples[i];
            float x2 = x * x;
            m_comp.rmsState += rmsCoeff * (x2 - m_comp.rmsState);
            float rmsDb = 10.0f * log10f(m_comp.rmsState + 1e-10f);
            float overDb = rmsDb - thresholdDb;
            float targetDb;
            if (overDb <= -kneeHalf) targetDb = 0.0f;
            else if (overDb >= kneeHalf)
                targetDb = -(overDb - kneeHalf) * slope;
            else {
                float t = (overDb + kneeHalf) / kneeDb;
                targetDb = -t * t * kneeHalf * slope;
            }
            float coeff = (targetDb < m_comp.envDb) ? attCoeff : relCoeff;
            m_comp.envDb += coeff * (targetDb - m_comp.envDb);
            samples[i] = x * makeupGain * powf(10.0f, m_comp.envDb / 20.0f);
        }
    }

    void processLimiter(float* samples, int numSamples) {
        const float ceiling = 0.89125094f;
        const float releaseCoeff = 0.995f;
        for (int i = 0; i < numSamples; i++) {
            float x = samples[i];
            float absX = fabsf(x);
            if (absX * m_limiterGain > ceiling)
                m_limiterGain = (std::min)(m_limiterGain,
                    ceiling / (absX + 1e-10f));
            else
                m_limiterGain += (1.0f - m_limiterGain) * (1.0f - releaseCoeff);
            samples[i] = x * m_limiterGain;
        }
    }
};
