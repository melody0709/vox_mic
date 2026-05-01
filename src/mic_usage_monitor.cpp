#include "mic_usage_monitor.h"
#include <cstdio>
#include <mmdeviceapi.h>
#include <audiopolicy.h>

#pragma comment(lib, "ole32.lib")

extern std::atomic<bool> g_micRequested;

MicUsageMonitor::MicUsageMonitor() {}

MicUsageMonitor::~MicUsageMonitor() {
    shutdown();
}

STDMETHODIMP MicUsageMonitor::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown)) {
        *ppv = static_cast<IUnknown*>(static_cast<IAudioSessionNotification*>(this));
    } else if (riid == __uuidof(IAudioSessionNotification)) {
        *ppv = static_cast<IAudioSessionNotification*>(this);
    } else if (riid == __uuidof(IAudioSessionEvents)) {
        *ppv = static_cast<IAudioSessionEvents*>(this);
    } else {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

STDMETHODIMP_(ULONG) MicUsageMonitor::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) MicUsageMonitor::Release() {
    return InterlockedDecrement(&m_refCount);
}

STDMETHODIMP MicUsageMonitor::OnSessionCreated(IAudioSessionControl *NewSession) {
    if (m_stopping.load(std::memory_order_relaxed)) return S_OK;
    registerEventsOnSession(NewSession);
    return S_OK;
}

STDMETHODIMP MicUsageMonitor::OnStateChanged(AudioSessionState NewState) {
    if (m_stopping.load(std::memory_order_relaxed)) return S_OK;

    if (NewState == AudioSessionStateActive) {
        if (m_activeCount.fetch_add(1, std::memory_order_relaxed) == 0) {
            g_micRequested.store(true, std::memory_order_relaxed);
        }
    } else if (NewState == AudioSessionStateInactive) {
        int prev = m_activeCount.fetch_sub(1, std::memory_order_relaxed);
        if (prev == 1) {
            g_micRequested.store(false, std::memory_order_relaxed);
        }
    }
    // AudioSessionStateExpired: ignore (always follows Inactive)
    return S_OK;
}

STDMETHODIMP MicUsageMonitor::OnSessionDisconnected(AudioSessionDisconnectReason DisconnectReason) {
    return S_OK;
}

void MicUsageMonitor::registerEventsOnSession(IAudioSessionControl* pSession) {
    if (!pSession) return;

    AudioSessionState state;
    HRESULT hr = pSession->GetState(&state);
    if (SUCCEEDED(hr) && state == AudioSessionStateActive) {
        m_activeCount.fetch_add(1, std::memory_order_relaxed);
        g_micRequested.store(true, std::memory_order_relaxed);
    }

    hr = pSession->RegisterAudioSessionNotification(static_cast<IAudioSessionEvents*>(this));
    if (SUCCEEDED(hr)) {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_registeredSessions.push_back(pSession);
    }
}

void MicUsageMonitor::unregisterAllSessions() {
    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    for (auto* pSession : m_registeredSessions) {
        pSession->UnregisterAudioSessionNotification(static_cast<IAudioSessionEvents*>(this));
    }
    m_registeredSessions.clear();
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

    IAudioSessionManager2* pSessionManager = nullptr;
    hr = pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, NULL, (void**)&pSessionManager);
    if (FAILED(hr)) {
        printf("MicUsageMonitor: Activate IAudioSessionManager2 failed 0x%08lx\n", hr);
        pDevice->Release(); m_pDevice = nullptr;
        pEnumerator->Release(); m_pEnumerator = nullptr;
        CoUninitialize();
        return false;
    }
    m_pSessionManager = pSessionManager;

    hr = pSessionManager->RegisterSessionNotification(
        static_cast<IAudioSessionNotification*>(this));
    if (FAILED(hr)) {
        printf("MicUsageMonitor: RegisterSessionNotification failed 0x%08lx\n", hr);
    }

    IAudioSessionEnumerator* pEnum = nullptr;
    hr = pSessionManager->GetSessionEnumerator(&pEnum);
    if (SUCCEEDED(hr)) {
        int count = 0;
        pEnum->GetCount(&count);
        for (int i = 0; i < count; i++) {
            IAudioSessionControl* pSessionControl = nullptr;
            if (SUCCEEDED(pEnum->GetSession(i, &pSessionControl))) {
                registerEventsOnSession(pSessionControl);
            }
        }
        pEnum->Release();
    }

    m_initialized = true;
    printf("MicUsageMonitor: initialized (event-driven)\n");
    return true;
}

void MicUsageMonitor::shutdown() {
    m_stopping.store(true, std::memory_order_relaxed);

    if (m_pSessionManager) {
        m_pSessionManager->UnregisterSessionNotification(
            static_cast<IAudioSessionNotification*>(this));
    }

    unregisterAllSessions();

    if (m_pSessionManager) {
        m_pSessionManager->Release();
        m_pSessionManager = nullptr;
    }
    if (m_pDevice) {
        m_pDevice->Release();
        m_pDevice = nullptr;
    }
    if (m_pEnumerator) {
        m_pEnumerator->Release();
        m_pEnumerator = nullptr;
    }
    if (m_initialized) {
        CoUninitialize();
        m_initialized = false;
    }
}
