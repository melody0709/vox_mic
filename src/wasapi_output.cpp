#include "wasapi_output.h"
#include "device_enum.h"
#include "dsp/pipeline.h"
#include <functiondiscoverykeys_devpkey.h>
#include <comdef.h>
#include <cstdio>
#include <cstring>
#include <atomic>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

extern std::atomic<bool> g_micRequested;
extern std::atomic<bool> g_demandMode;

WASAPIOutput::WASAPIOutput() {}

WASAPIOutput::~WASAPIOutput() {
    stop();
    if (m_hEvent) { CloseHandle(m_hEvent); m_hEvent = nullptr; }
    if (m_pWaveFormat) { CoTaskMemFree(m_pWaveFormat); m_pWaveFormat = nullptr; }
    if (m_pRenderClient) { m_pRenderClient->Release(); m_pRenderClient = nullptr; }
    if (m_pAudioClient) { m_pAudioClient->Release(); m_pAudioClient = nullptr; }
    if (m_pDevice) { m_pDevice->Release(); m_pDevice = nullptr; }
    if (m_pEnumerator) { m_pEnumerator->Release(); m_pEnumerator = nullptr; }
    CoUninitialize();
}

bool WASAPIOutput::initCOM() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        printf("COM init failed: 0x%08lx\n", hr);
        return false;
    }
    return true;
}

bool WASAPIOutput::initDeviceEnumerator() {
    HRESULT hr = CoCreateInstance(
        __uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&m_pEnumerator);
    if (FAILED(hr)) {
        printf("CoCreateInstance failed: 0x%08lx\n", hr);
        return false;
    }
    return true;
}

bool WASAPIOutput::findAndInitDevice() {
    m_pDevice = findVBCableDevice(m_pEnumerator);
    if (!m_pDevice) {
        printf("VB-CABLE not found, using default\n");
        m_pDevice = getDefaultRenderDevice(m_pEnumerator);
    }
    return m_pDevice != nullptr;
}

bool WASAPIOutput::initAudioClient(IMMDevice* pDevice) {
    HRESULT hr = pDevice->Activate(
        __uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&m_pAudioClient);
    if (FAILED(hr)) {
        printf("Activate failed: 0x%08lx\n", hr);
        return false;
    }

    // Get mix format and use it directly
    hr = m_pAudioClient->GetMixFormat(&m_pWaveFormat);
    if (FAILED(hr)) {
        printf("GetMixFormat failed: 0x%08lx\n", hr);
        return false;
    }

    printf("Device format: %u Hz, %u ch, %u bits\n",
        m_pWaveFormat->nSamplesPerSec, m_pWaveFormat->nChannels, m_pWaveFormat->wBitsPerSample);

    REFERENCE_TIME hnsBufferDuration = 100000; // 10ms
    hr = m_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, hnsBufferDuration, 0, m_pWaveFormat, NULL);
    if (FAILED(hr)) {
        printf("Initialize failed: 0x%08lx\n", hr);
        return false;
    }

    m_hEvent = CreateEventEx(NULL, NULL, 0, EVENT_ALL_ACCESS);
    if (!m_hEvent) {
        printf("CreateEvent failed\n");
        return false;
    }
    hr = m_pAudioClient->SetEventHandle(m_hEvent);
    if (FAILED(hr)) {
        printf("SetEventHandle failed: 0x%08lx\n", hr);
        return false;
    }

    hr = m_pAudioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) {
        printf("GetBufferSize failed: 0x%08lx\n", hr);
        return false;
    }

    printf("Buffer: %u frames (%.1f ms)\n",
        m_bufferFrameCount, (float)m_bufferFrameCount / m_pWaveFormat->nSamplesPerSec * 1000.0f);
    return true;
}

bool WASAPIOutput::initRenderClient() {
    HRESULT hr = m_pAudioClient->GetService(
        __uuidof(IAudioRenderClient), (void**)&m_pRenderClient);
    if (FAILED(hr)) {
        printf("GetService failed: 0x%08lx\n", hr);
        return false;
    }
    return true;
}

bool WASAPIOutput::init(bool listDevicesOnly) {
    if (!initCOM()) return false;
    if (!initDeviceEnumerator()) return false;
    if (listDevicesOnly) { listAudioDevices(m_pEnumerator); return true; }
    if (!findAndInitDevice()) return false;
    if (!initAudioClient(m_pDevice)) return false;
    if (!initRenderClient()) return false;

    m_deviceSampleRate = m_pWaveFormat->nSamplesPerSec;
    m_deviceChannels = m_pWaveFormat->nChannels;
    m_deviceBits = m_pWaveFormat->wBitsPerSample;
    m_resampleRatio = (double)SAMPLE_RATE / (double)m_deviceSampleRate;

    printf("Resample: %dHz -> %uHz (ratio=%.4f)\n",
        SAMPLE_RATE, m_deviceSampleRate, m_resampleRatio);
    printf("WASAPI ready\n");
    return true;
}

void WASAPIOutput::renderThread() {
    int16_t monoBuffer[FRAMES_PER_BLOCK];
    float floatBuf[FRAMES_PER_BLOCK];
    UINT32 outFrames = (UINT32)((uint64_t)FRAMES_PER_BLOCK * m_deviceSampleRate / SAMPLE_RATE);
    UINT32 outBytesPerFrame = m_deviceChannels * (m_deviceBits / 8);
    UINT32 outBlockSize = outFrames * outBytesPerFrame;
    bool isFloat = (m_deviceBits == 32);

    DspPipeline pipeline;
    pipeline.init((float)SAMPLE_RATE);

    printf("Render: %d input frames -> %u output frames (event-driven)\n", FRAMES_PER_BLOCK, outFrames);
    fflush(stdout);

    {
        UINT32 framesRemaining = m_bufferFrameCount;
        while (framesRemaining > 0) {
            UINT32 fillFrames = (framesRemaining < outFrames) ? framesRemaining : outFrames;
            UINT32 fillBytes = fillFrames * outBytesPerFrame;
            BYTE* pData = nullptr;
            HRESULT hr = m_pRenderClient->GetBuffer(fillFrames, &pData);
            if (FAILED(hr)) break;
            memset(pData, 0, fillBytes);
            m_pRenderClient->ReleaseBuffer(fillFrames, 0);
            framesRemaining -= fillFrames;
        }
    }

    HRESULT hr = m_pAudioClient->Start();
    if (FAILED(hr)) {
        printf("Start failed: 0x%08lx\n", hr);
        return;
    }

    printf("Audio render started (event-driven)\n");
    fflush(stdout);

    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);
    double localProcUsEma = 0;
    size_t procCount = 0;

    while (m_running.load(std::memory_order_relaxed)) {
        DWORD waitResult = WaitForSingleObject(m_hEvent, 2000);
        if (waitResult != WAIT_OBJECT_0) {
            int sc = renderStallScore.load(std::memory_order_relaxed);
            if (sc < 3) renderStallScore.store(sc + 1, std::memory_order_relaxed);
            if (!m_running.load(std::memory_order_relaxed)) break;
            continue;
        }
        renderStallScore.store(0, std::memory_order_relaxed);

        float gain = g_gain.load(std::memory_order_relaxed);

        UINT32 padding = 0;
        hr = m_pAudioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) break;

        UINT32 avail = m_bufferFrameCount - padding;

        while (avail >= outFrames && m_running.load(std::memory_order_relaxed)) {
            BYTE* pData = nullptr;
            hr = m_pRenderClient->GetBuffer(outFrames, &pData);
            if (FAILED(hr)) break;

            LARGE_INTEGER t1;
            QueryPerformanceCounter(&t1);

            if (m_ringBuffer.pop((uint8_t*)monoBuffer, BLOCK_SIZE)) {
                for (int i = 0; i < FRAMES_PER_BLOCK; i++)
                    floatBuf[i] = (float)monoBuffer[i] / 32768.0f;

                pipeline.process(floatBuf, FRAMES_PER_BLOCK, (float)SAMPLE_RATE);

                if (isFloat) {
                    float* out = (float*)pData;
                    for (UINT32 i = 0; i < outFrames; i++) {
                        double srcPos = (double)i * m_resampleRatio;
                        UINT32 idx = (UINT32)srcPos;
                        double frac = srcPos - idx;
                        float s;
                        if (idx + 1 < FRAMES_PER_BLOCK)
                            s = (float)((1.0 - frac) * floatBuf[idx] + frac * floatBuf[idx + 1]);
                        else if (idx < FRAMES_PER_BLOCK)
                            s = floatBuf[idx];
                        else
                            s = 0.0f;
                        out[i * m_deviceChannels] = s * gain;
                        if (m_deviceChannels > 1) out[i * m_deviceChannels + 1] = s * gain;
                    }
                } else {
                    int16_t* out = (int16_t*)pData;
                    for (UINT32 i = 0; i < outFrames; i++) {
                        double srcPos = (double)i * m_resampleRatio;
                        UINT32 idx = (UINT32)srcPos;
                        double frac = srcPos - idx;
                        double s;
                        if (idx + 1 < FRAMES_PER_BLOCK)
                            s = (1.0 - frac) * floatBuf[idx] + frac * floatBuf[idx + 1];
                        else if (idx < FRAMES_PER_BLOCK)
                            s = (double)floatBuf[idx];
                        else
                            s = 0.0;
                        s *= gain * 32767.0;
                        s = (s < -32768.0) ? -32768.0 : (s > 32767.0) ? 32767.0 : s;
                        int16_t sample = (int16_t)s;
                        out[i * m_deviceChannels] = sample;
                        if (m_deviceChannels > 1) out[i * m_deviceChannels + 1] = sample;
                    }
                }
            } else {
                memset(pData, 0, outBlockSize);
                bool expectAudio = !g_demandMode.load(std::memory_order_relaxed) ||
                                   g_micRequested.load(std::memory_order_relaxed);
                if (expectAudio) {
                    underruns.fetch_add(1, std::memory_order_relaxed);
                } else {
                    idleSilenceBlocks.fetch_add(1, std::memory_order_relaxed);
                }
            }

            m_pRenderClient->ReleaseBuffer(outFrames, 0);
            avail -= outFrames;

            LARGE_INTEGER t2;
            QueryPerformanceCounter(&t2);
            double procUs = (double)(t2.QuadPart - t1.QuadPart) * 1e6 / (double)qpcFreq.QuadPart;
            localProcUsEma = (localProcUsEma * 0.95) + (procUs * 0.05);
            procCount++;

            size_t q = m_ringBuffer.sizeBlocks(BLOCK_SIZE);
            double est = (double)q * 10.0 + localProcUsEma * 0.001 + 11.0;
            estLatencyMs.store(est, std::memory_order_relaxed);
            procUsEma.store(localProcUsEma, std::memory_order_relaxed);
        }

        if (FAILED(hr)) break;
    }

    m_pAudioClient->Stop();
    printf("Audio render stopped\n");
}

void WASAPIOutput::start() {
    if (m_running.load()) return;
    m_running.store(true);
    m_renderThread = std::thread(&WASAPIOutput::renderThread, this);
}

void WASAPIOutput::stop() {
    if (!m_running.load()) return;
    m_running.store(false);
    if (m_renderThread.joinable()) m_renderThread.join();
}
