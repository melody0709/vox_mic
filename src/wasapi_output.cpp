#include "wasapi_output.h"
#include "device_enum.h"
#include <functiondiscoverykeys_devpkey.h>
#include <comdef.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

WASAPIOutput::WASAPIOutput() {}

WASAPIOutput::~WASAPIOutput() {
    stop();
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

    REFERENCE_TIME hnsBufferDuration = 2000000; // 200ms (was 1s)
    hr = m_pAudioClient->Initialize(
        AUDCLNT_SHAREMODE_SHARED, 0, hnsBufferDuration, 0, m_pWaveFormat, NULL);
    if (FAILED(hr)) {
        printf("Initialize failed: 0x%08lx\n", hr);
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
    UINT32 outFrames = (UINT32)((uint64_t)FRAMES_PER_BLOCK * m_deviceSampleRate / SAMPLE_RATE);
    UINT32 outBytesPerFrame = m_deviceChannels * (m_deviceBits / 8);
    UINT32 outBlockSize = outFrames * outBytesPerFrame;

    printf("Render: %d input frames -> %u output frames\n", FRAMES_PER_BLOCK, outFrames);
    fflush(stdout);

    HRESULT hr = m_pAudioClient->Start();
    if (FAILED(hr)) {
        printf("Start failed: 0x%08lx\n", hr);
        return;
    }

    printf("Audio render started\n");
    fflush(stdout);

    bool isFloat = (m_deviceBits == 32);

    while (m_running.load(std::memory_order_relaxed)) {
        UINT32 padding = 0;
        hr = m_pAudioClient->GetCurrentPadding(&padding);
        if (FAILED(hr)) break;

        if (m_bufferFrameCount - padding < outFrames) {
            Sleep(1);
            continue;
        }

        BYTE* pData = nullptr;
        hr = m_pRenderClient->GetBuffer(outFrames, &pData);
        if (FAILED(hr)) {
            printf("GetBuffer failed: 0x%08lx\n", hr);
            break;
        }

        if (m_ringBuffer.pop((uint8_t*)monoBuffer, BLOCK_SIZE)) {
            // Resample mono int16 -> device format stereo
            if (isFloat) {
                float* out = (float*)pData;
                for (UINT32 i = 0; i < outFrames; i++) {
                    double srcPos = (double)i * m_resampleRatio;
                    UINT32 idx = (UINT32)srcPos;
                    double frac = srcPos - idx;
                    float s;
                    if (idx + 1 < FRAMES_PER_BLOCK)
                        s = (float)((1.0 - frac) * monoBuffer[idx] + frac * monoBuffer[idx + 1]) / 32768.0f;
                    else if (idx < FRAMES_PER_BLOCK)
                        s = (float)monoBuffer[idx] / 32768.0f;
                    else
                        s = 0.0f;
                    out[i * m_deviceChannels] = s;
                    if (m_deviceChannels > 1) out[i * m_deviceChannels + 1] = s;
                }
            } else {
                int16_t* out = (int16_t*)pData;
                for (UINT32 i = 0; i < outFrames; i++) {
                    UINT32 idx = (UINT32)((double)i * m_resampleRatio);
                    if (idx >= FRAMES_PER_BLOCK) idx = FRAMES_PER_BLOCK - 1;
                    out[i * m_deviceChannels] = monoBuffer[idx];
                    if (m_deviceChannels > 1) out[i * m_deviceChannels + 1] = monoBuffer[idx];
                }
            }
        } else {
            memset(pData, 0, outBlockSize);
            underruns.fetch_add(1, std::memory_order_relaxed);
        }

        m_pRenderClient->ReleaseBuffer(outFrames, 0);
        // Sleep for full block duration to match input rate
        Sleep((DWORD)(outFrames * 1000 / m_deviceSampleRate));
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
