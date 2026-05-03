#pragma once
#include <atomic>
#include <algorithm>

extern "C" {
#include "rnnoise.h"
}

extern std::atomic<float> g_gain;
extern std::atomic<bool> g_eqEnabled;
extern std::atomic<float> g_eqPresence;
extern std::atomic<float> g_eqBassCut;
extern std::atomic<bool> g_compressorEnabled;
extern std::atomic<bool> g_nrEnabled;
extern std::atomic<float> g_nrStrength;

#include "dsp/biquad.h"

class DspPipeline {
public:
    ~DspPipeline() {
        if (m_rnnoise) rnnoise_destroy(m_rnnoise);
    }

    void init(float sampleRate) {
        if (!m_rnnoise) m_rnnoise = rnnoise_create(NULL);
        updateSettings(sampleRate);
    }

    void updateSettings(float sampleRate) {
        m_nrActive = g_nrEnabled.load(std::memory_order_relaxed);
        if (m_rnnoise)
            rnnoise_set_strength(m_rnnoise, g_nrStrength.load(std::memory_order_relaxed));
        bool eqOn = g_eqEnabled.load(std::memory_order_relaxed);
        float presence = g_eqPresence.load(std::memory_order_relaxed);
        float bassCut = g_eqBassCut.load(std::memory_order_relaxed);
        bool compOn = g_compressorEnabled.load(std::memory_order_relaxed);

        if (eqOn != m_eqActive || presence != m_lastPresence || bassCut != m_lastBassCut) {
            m_eqActive = eqOn;
            m_lastPresence = presence;
            m_lastBassCut = bassCut;
            if (eqOn) {
                m_bq[0].setHPF(80.0f, 0.707f, sampleRate);
                m_bq[1].setLowShelf(120.0f, bassCut, 0.7f, sampleRate);
                m_bq[2].setPeak(250.0f, bassCut * 0.5f, 0.6f, sampleRate);
                m_bq[3].setPeak(2500.0f, presence, 0.5f, sampleRate);
                m_bq[4].setPeak(3200.0f, presence * 0.6f, 0.7f, sampleRate);
                m_bq[5].setHighShelf(8000.0f, 1.5f, 0.7f, sampleRate);
            }
            for (int i = 0; i < 6; i++) m_bq[i].reset();
        }
        m_compActive = compOn;
        if (compOn) {
            m_comp.rmsState = 0.0f;
            m_comp.envDb = 0.0f;
        }
    }

    void process(float* samples, int numSamples, float sampleRate) {
        if (numSamples != 480) {
            processFallback(samples, numSamples, sampleRate);
            return;
        }

        bool nrOn = g_nrEnabled.load(std::memory_order_relaxed);
        if (nrOn != m_nrActive) updateSettings(sampleRate);
        if (nrOn && m_rnnoise) {
            rnnoise_process_frame(m_rnnoise, samples, samples);
        }

        bool eqOn = g_eqEnabled.load(std::memory_order_relaxed);
        bool compOn = g_compressorEnabled.load(std::memory_order_relaxed);
        float presence = g_eqPresence.load(std::memory_order_relaxed);
        float bassCut = g_eqBassCut.load(std::memory_order_relaxed);

        if (eqOn != m_eqActive || presence != m_lastPresence || bassCut != m_lastBassCut)
            updateSettings(sampleRate);

        if (eqOn) {
            for (int i = 0; i < numSamples; i++) {
                float x = samples[i];
                x = m_bq[0].process(x);
                x = m_bq[1].process(x);
                x = m_bq[2].process(x);
                x = m_bq[3].process(x);
                x = m_bq[4].process(x);
                x = m_bq[5].process(x);
                samples[i] = x;
            }
        }

        if (compOn) processCompressor(samples, numSamples);
        processLimiter(samples, numSamples);
    }

private:
    void processFallback(float* samples, int numSamples, float sampleRate) {
        (void)sampleRate;
        bool eqOn = g_eqEnabled.load(std::memory_order_relaxed);
        bool compOn = g_compressorEnabled.load(std::memory_order_relaxed);
        if (eqOn) {
            for (int i = 0; i < numSamples; i++) {
                float x = samples[i];
                for (int j = 0; j < 6; j++) x = m_bq[j].process(x);
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
    bool m_nrActive = true;
    bool m_eqActive = true;
    float m_lastPresence = 3.0f;
    float m_lastBassCut = -3.0f;
    bool m_compActive = true;
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
        const float rmsCoeff = expf(-1.0f / (0.001f * rmsMs * 48000.0f / numSamples));
        const float attCoeff = expf(-1.0f / (0.001f * attackMs * 48000.0f));
        const float relCoeff = expf(-1.0f / (0.001f * releaseMs * 48000.0f));
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
            else if (overDb >= kneeHalf) targetDb = -(overDb - kneeHalf) * slope;
            else { float t = (overDb + kneeHalf) / kneeDb; targetDb = -t * t * kneeHalf * slope; }
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
                m_limiterGain = (std::min)(m_limiterGain, ceiling / (absX + 1e-10f));
            else
                m_limiterGain += (1.0f - m_limiterGain) * (1.0f - releaseCoeff);
            samples[i] = x * m_limiterGain;
        }
    }
};
