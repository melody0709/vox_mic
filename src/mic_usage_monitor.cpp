#include "mic_usage_monitor.h"
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <cstdio>

#pragma comment(lib, "ole32.lib")

MicUsageMonitor::MicUsageMonitor() {}

MicUsageMonitor::~MicUsageMonitor() {
    shutdown();
}

bool MicUsageMonitor::init() {
    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        printf("MicUsageMonitor: COM init failed 0x%08lx\n", hr);
        return false;
    }

    IMMDeviceEnumerator* pEnumerator = nullptr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    if (FAILED(hr)) {
        printf("MicUsageMonitor: CoCreateInstance failed 0x%08lx\n", hr);
        CoUninitialize();
        return false;
    }
    m_pEnumerator = pEnumerator;

    IMMDevice* pDevice = nullptr;
    hr = pEnumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &pDevice);
    if (FAILED(hr)) {
        printf("MicUsageMonitor: GetDefaultAudioEndpoint failed 0x%08lx\n", hr);
        pEnumerator->Release();
        m_pEnumerator = nullptr;
        CoUninitialize();
        return false;
    }
    m_pDevice = pDevice;

    m_initialized = true;
    printf("MicUsageMonitor: initialized\n");
    return true;
}

void MicUsageMonitor::shutdown() {
    if (m_pDevice) {
        ((IMMDevice*)m_pDevice)->Release();
        m_pDevice = nullptr;
    }
    if (m_pEnumerator) {
        ((IMMDeviceEnumerator*)m_pEnumerator)->Release();
        m_pEnumerator = nullptr;
    }
    if (m_initialized) {
        CoUninitialize();
        m_initialized = false;
    }
}

bool MicUsageMonitor::isCaptureActive() {
    if (!m_initialized || !m_pDevice)
        return false;

    IMMDevice* pDevice = (IMMDevice*)m_pDevice;

    IAudioSessionManager2* pSessionManager = nullptr;
    HRESULT hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
    if (FAILED(hr)) return false;

    IAudioSessionEnumerator* pEnumerator = nullptr;
    hr = pSessionManager->GetSessionEnumerator(&pEnumerator);
    pSessionManager->Release();
    if (FAILED(hr)) return false;

    int count = 0;
    hr = pEnumerator->GetCount(&count);
    if (FAILED(hr)) {
        pEnumerator->Release();
        return false;
    }

    bool active = false;
    for (int i = 0; i < count; i++) {
        IAudioSessionControl* pSessionControl = nullptr;
        hr = pEnumerator->GetSession(i, &pSessionControl);
        if (FAILED(hr)) continue;

        AudioSessionState state;
        hr = pSessionControl->GetState(&state);
        pSessionControl->Release();

        if (SUCCEEDED(hr) && state == AudioSessionStateActive) {
            active = true;
            break;
        }
    }

    pEnumerator->Release();
    return active;
}
