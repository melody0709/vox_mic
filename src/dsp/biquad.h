#pragma once
#include <cmath>

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;

    void setHPF(float freq, float q, float sr) {
        float w = 2.0f * 3.14159265f * freq / sr;
        float cosW = cosf(w), sinW = sinf(w);
        float alpha = sinW / (2.0f * q);
        float a0 = 1.0f + alpha;
        float b0_ = (1.0f + cosW) / 2.0f;
        b0 = b0_ / a0;
        b1 = -(1.0f + cosW) / a0;
        b2 = b0;
        a1 = (-2.0f * cosW) / a0;
        a2 = (1.0f - alpha) / a0;
    }

    void setLowShelf(float freq, float dbGain, float q, float sr) {
        float a = powf(10.0f, dbGain / 40.0f);
        float w = 2.0f * 3.14159265f * freq / sr;
        float cosW = cosf(w), sinW = sinf(w);
        float beta = 2.0f * sqrtf(a) * sinW / (2.0f * q);
        float a0 = (a + 1.0f) + (a - 1.0f) * cosW + beta;
        b0 = a * ((a + 1.0f) - (a - 1.0f) * cosW + beta) / a0;
        b1 = 2.0f * a * ((a - 1.0f) - (a + 1.0f) * cosW) / a0;
        b2 = a * ((a + 1.0f) - (a - 1.0f) * cosW - beta) / a0;
        a1 = -2.0f * ((a - 1.0f) + (a + 1.0f) * cosW) / a0;
        a2 = ((a + 1.0f) + (a - 1.0f) * cosW - beta) / a0;
    }

    void setPeak(float freq, float dbGain, float q, float sr) {
        float a = powf(10.0f, dbGain / 40.0f);
        float w = 2.0f * 3.14159265f * freq / sr;
        float cosW = cosf(w), sinW = sinf(w);
        float alpha = sinW / (2.0f * q);
        float a0 = 1.0f + alpha / a;
        b0 = (1.0f + alpha * a) / a0;
        b1 = (-2.0f * cosW) / a0;
        b2 = (1.0f - alpha * a) / a0;
        a1 = b1;
        a2 = (1.0f - alpha / a) / a0;
    }

    void setHighShelf(float freq, float dbGain, float q, float sr) {
        float a = powf(10.0f, dbGain / 40.0f);
        float w = 2.0f * 3.14159265f * freq / sr;
        float cosW = cosf(w), sinW = sinf(w);
        float beta = 2.0f * sqrtf(a) * sinW / (2.0f * q);
        float a0 = (a + 1.0f) - (a - 1.0f) * cosW + beta;
        b0 = a * ((a + 1.0f) + (a - 1.0f) * cosW + beta) / a0;
        b1 = -2.0f * a * ((a - 1.0f) + (a + 1.0f) * cosW) / a0;
        b2 = a * ((a + 1.0f) + (a - 1.0f) * cosW - beta) / a0;
        a1 = 2.0f * ((a - 1.0f) - (a + 1.0f) * cosW) / a0;
        a2 = ((a + 1.0f) - (a - 1.0f) * cosW - beta) / a0;
    }

    float process(float x) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() { z1 = 0.0f; z2 = 0.0f; }
};
