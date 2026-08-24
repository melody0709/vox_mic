#include "mic_usage_monitor.h"

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <functiondiscoverykeys_devpkey.h>
#include <utility>

#pragma comment(lib, "ole32.lib")

extern std::atomic<bool> g_micRequested;
extern std::atomic<bool> g_demandMode;
extern std::atomic<uint64_t> g_micOnTick;

namespace {

constexpr uint64_t kDeactivateGraceMs = 400;

enum class PendingStateSource {
    None,
    Register,
    Event,
    Reconcile,
    Disconnected
};

const char* stateSourceName(PendingStateSource source) {
    switch (source) {
    case PendingStateSource::Register: return "register";
    case PendingStateSource::Event: return "event";
    case PendingStateSource::Reconcile: return "reconcile";
    case PendingStateSource::Disconnected: return "disconnected";
    case PendingStateSource::None:
    default:
        return "none";
    }
}

const char* sessionStateName(AudioSessionState state) {
    switch (state) {
    case AudioSessionStateInactive: return "inactive";
    case AudioSessionStateActive: return "active";
    case AudioSessionStateExpired: return "expired";
    default: return "unknown";
    }
}

MicSessionActivity toTrackerState(AudioSessionState state) {
    switch (state) {
    case AudioSessionStateActive: return MicSessionActivity::Active;
    case AudioSessionStateExpired: return MicSessionActivity::Expired;
    case AudioSessionStateInactive:
    default:
        return MicSessionActivity::Inactive;
    }
}

std::wstring getSessionKey(IAudioSessionControl* session, DWORD* processId) {
    if (processId) *processId = 0;

    IAudioSessionControl2* control2 = nullptr;
    if (SUCCEEDED(session->QueryInterface(__uuidof(IAudioSessionControl2),
            reinterpret_cast<void**>(&control2)))) {
        if (processId) control2->GetProcessId(processId);

        LPWSTR instanceId = nullptr;
        if (SUCCEEDED(control2->GetSessionInstanceIdentifier(&instanceId)) && instanceId) {
            std::wstring key(instanceId);
            CoTaskMemFree(instanceId);
            control2->Release();
            return key;
        }

        LPWSTR sessionId = nullptr;
        if (SUCCEEDED(control2->GetSessionIdentifier(&sessionId)) && sessionId) {
            std::wstring key(sessionId);
            CoTaskMemFree(sessionId);
            control2->Release();
            return key;
        }
        control2->Release();
    }

    wchar_t fallback[64]{};
    swprintf_s(fallback, L"session-%p", static_cast<void*>(session));
    return fallback;
}

} // namespace

class MicUsageMonitor::SessionObserver : public IAudioSessionEvents {
public:
    SessionObserver(MicUsageMonitor* owner, IAudioSessionControl* control,
        std::wstring key, DWORD processId)
        : m_owner(owner), m_control(control), m_key(std::move(key)),
          m_processId(processId) {
        m_control->AddRef();
    }

    bool start() {
        HRESULT hr = m_control->RegisterAudioSessionNotification(this);
        if (FAILED(hr)) {
            printf("MicUsageMonitor: session callback registration failed pid=%lu hr=0x%08lx\n",
                static_cast<unsigned long>(m_processId), hr);
            fflush(stdout);
            return false;
        }
        m_registered.store(true, std::memory_order_release);

        // Query after registering. If the state changed during registration,
        // the callback and this query converge through idempotent per-session state.
        refreshState(PendingStateSource::Register);
        return true;
    }

    void detachOwner() {
        m_owner.store(nullptr, std::memory_order_release);
    }

    void unregisterCallback() {
        if (m_registered.exchange(false, std::memory_order_acq_rel)) {
            m_control->UnregisterAudioSessionNotification(this);
        }
    }

    void refreshState(PendingStateSource source) {
        AudioSessionState state = AudioSessionStateInactive;
        HRESULT hr = m_control->GetState(&state);
        if (FAILED(hr)) return;
        applyState(state, source);
    }

    const std::wstring& key() const { return m_key; }
    DWORD processId() const { return m_processId; }
    AudioSessionState state() const {
        return static_cast<AudioSessionState>(m_state.load(std::memory_order_acquire));
    }
    PendingStateSource consumeSource() {
        return static_cast<PendingStateSource>(
            m_pendingSource.exchange(
                static_cast<int>(PendingStateSource::None),
                std::memory_order_acq_rel));
    }
    bool consumeSawActive() {
        return m_sawActive.exchange(false, std::memory_order_acq_rel);
    }

    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionEvents)) {
            *ppv = static_cast<IAudioSessionEvents*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override {
        return InterlockedIncrement(&m_refCount);
    }

    STDMETHODIMP_(ULONG) Release() override {
        ULONG value = InterlockedDecrement(&m_refCount);
        if (value == 0) delete this;
        return value;
    }

    STDMETHODIMP OnDisplayNameChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnIconPathChanged(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnSimpleVolumeChanged(float, BOOL, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnChannelVolumeChanged(DWORD, float*, DWORD, LPCGUID) override { return S_OK; }
    STDMETHODIMP OnGroupingParamChanged(LPCGUID, LPCGUID) override { return S_OK; }

    STDMETHODIMP OnStateChanged(AudioSessionState newState) override {
        applyState(newState, PendingStateSource::Event);
        return S_OK;
    }

    STDMETHODIMP OnSessionDisconnected(AudioSessionDisconnectReason) override {
        applyState(AudioSessionStateExpired, PendingStateSource::Disconnected);
        return S_OK;
    }

private:
    ~SessionObserver() {
        unregisterCallback();
        m_control->Release();
    }

    void applyState(AudioSessionState newState, PendingStateSource source) {
        const int previous = m_state.exchange(
            static_cast<int>(newState), std::memory_order_acq_rel);
        if (previous == static_cast<int>(newState)) return;

        if (newState == AudioSessionStateActive) {
            m_sawActive.store(true, std::memory_order_release);
        }
        m_pendingSource.store(static_cast<int>(source), std::memory_order_release);

        MicUsageMonitor* owner = m_owner.load(std::memory_order_acquire);
        if (owner) owner->notifyObserverStateChanged();
    }

    LONG m_refCount{1};
    std::atomic<MicUsageMonitor*> m_owner{nullptr};
    IAudioSessionControl* m_control{nullptr};
    std::wstring m_key;
    DWORD m_processId{0};
    std::atomic<int> m_state{static_cast<int>(AudioSessionStateInactive)};
    std::atomic<int> m_pendingSource{static_cast<int>(PendingStateSource::None)};
    std::atomic<bool> m_sawActive{false};
    std::atomic<bool> m_registered{false};
};

MicUsageMonitor::MicUsageMonitor() = default;

MicUsageMonitor::~MicUsageMonitor() {
    shutdown();
}

STDMETHODIMP MicUsageMonitor::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioSessionNotification)) {
        *ppv = static_cast<IAudioSessionNotification*>(this);
        AddRef();
        return S_OK;
    }
    *ppv = nullptr;
    return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) MicUsageMonitor::AddRef() {
    return InterlockedIncrement(&m_refCount);
}

STDMETHODIMP_(ULONG) MicUsageMonitor::Release() {
    return InterlockedDecrement(&m_refCount);
}

STDMETHODIMP MicUsageMonitor::OnSessionCreated(IAudioSessionControl* newSession) {
    // Do not call COM methods or take locks in a session callback. Wake the
    // owner thread; it will enumerate and register the new session there.
    if (newSession && !m_stopping.load(std::memory_order_acquire) && m_wakeEvent) {
        SetEvent(m_wakeEvent);
    }
    return S_OK;
}

void MicUsageMonitor::notifyObserverStateChanged() {
    if (m_wakeEvent) SetEvent(m_wakeEvent);
}

void MicUsageMonitor::syncSessionMetricsLocked() {
    m_activeSessionCount.store(
        static_cast<int>(m_sessionStates.activeCount()), std::memory_order_relaxed);
    m_trackedSessionCount.store(
        static_cast<int>(m_sessionStates.trackedCount()), std::memory_order_relaxed);
}

void MicUsageMonitor::setMicRequestedLocked(bool requested, const char* reason) {
    const bool previous = g_micRequested.exchange(requested, std::memory_order_acq_rel);
    if (previous == requested) return;

    if (requested) {
        g_micOnTick.store(GetTickCount64(), std::memory_order_release);
    } else {
        g_micOnTick.store(0, std::memory_order_release);
    }

    printf("[Demand] requested=%d reason=%s active=%zu tracked=%zu corrections=%llu\n",
        requested ? 1 : 0, reason,
        m_sessionStates.activeCount(), m_sessionStates.trackedCount(),
        static_cast<unsigned long long>(
            m_reconciliationCorrections.load(std::memory_order_relaxed)));
    fflush(stdout);
}

void MicUsageMonitor::evaluateRequestedStateLocked(const char* reason) {
    if (!g_demandMode.load(std::memory_order_acquire)) {
        m_inactiveDeadlineTick = 0;
        setMicRequestedLocked(true, "demand-disabled");
        return;
    }

    if (m_sessionStates.activeCount() > 0) {
        m_inactiveDeadlineTick = 0;
        setMicRequestedLocked(true, reason);
        return;
    }

    if (!g_micRequested.load(std::memory_order_acquire)) {
        m_inactiveDeadlineTick = 0;
        return;
    }

    const uint64_t now = GetTickCount64();
    if (m_inactiveDeadlineTick == 0) {
        m_inactiveDeadlineTick = now + kDeactivateGraceMs;
        return;
    }
    if (now >= m_inactiveDeadlineTick) {
        m_inactiveDeadlineTick = 0;
        setMicRequestedLocked(false, "all-sessions-inactive");
    }
}

void MicUsageMonitor::processObserverState(SessionObserver* observer) {
    const PendingStateSource source = observer->consumeSource();
    const bool sawActive = observer->consumeSawActive();
    if (source == PendingStateSource::None && !sawActive) return;

    std::lock_guard<std::mutex> lock(m_sessionsMutex);
    if (m_stopping.load(std::memory_order_acquire)) return;
    if (std::find(m_sessions.begin(), m_sessions.end(), observer) == m_sessions.end()) {
        return;
    }

    const AudioSessionState newState = observer->state();
    if (sawActive && newState != AudioSessionStateActive) {
        const auto pulseResult = m_sessionStates.update(
            observer->key(), MicSessionActivity::Active);
        if (pulseResult.stateChanged) {
            syncSessionMetricsLocked();
            printf("[MicSession] pid=%lu state=active source=event-pulse active=%zu tracked=%zu\n",
                static_cast<unsigned long>(observer->processId()),
                pulseResult.activeCount, pulseResult.trackedCount);
            fflush(stdout);
            evaluateRequestedStateLocked("event-pulse");
        }
    }

    const auto result = m_sessionStates.update(
        observer->key(), toTrackerState(newState));
    if (!result.stateChanged) return;

    if (source == PendingStateSource::Reconcile) {
        m_reconciliationCorrections.fetch_add(1, std::memory_order_relaxed);
    }
    syncSessionMetricsLocked();

    printf("[MicSession] pid=%lu state=%s source=%s active=%zu tracked=%zu\n",
        static_cast<unsigned long>(observer->processId()),
        sessionStateName(newState), stateSourceName(source),
        result.activeCount, result.trackedCount);
    fflush(stdout);

    evaluateRequestedStateLocked(stateSourceName(source));
}

void MicUsageMonitor::registerEventsOnSession(IAudioSessionControl* session) {
    if (!session || m_stopping.load(std::memory_order_acquire)) return;

    DWORD processId = 0;
    std::wstring key = getSessionKey(session, &processId);
    SessionObserver* observer = new SessionObserver(
        this, session, key, processId);

    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        if (m_stopping.load(std::memory_order_acquire) ||
            m_sessionStates.contains(key)) {
            observer->detachOwner();
            observer->Release();
            return;
        }
        m_sessions.push_back(observer);
        m_sessionStates.update(key, MicSessionActivity::Inactive);
        syncSessionMetricsLocked();
    }

    if (observer->start()) return;

    observer->detachOwner();
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = std::find(m_sessions.begin(), m_sessions.end(), observer);
        if (it != m_sessions.end()) m_sessions.erase(it);
        m_sessionStates.remove(key);
        syncSessionMetricsLocked();
        evaluateRequestedStateLocked("registration-failed");
    }
    observer->Release();
}

void MicUsageMonitor::cleanupExpiredSessions() {
    std::vector<SessionObserver*> expired;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.begin();
        while (it != m_sessions.end()) {
            SessionObserver* observer = *it;
            if (observer->state() != AudioSessionStateExpired) {
                ++it;
                continue;
            }
            observer->detachOwner();
            m_sessionStates.remove(observer->key());
            expired.push_back(observer);
            it = m_sessions.erase(it);
        }
        syncSessionMetricsLocked();
        evaluateRequestedStateLocked("session-expired");
    }

    for (SessionObserver* observer : expired) {
        observer->unregisterCallback();
        observer->Release();
    }
}

void MicUsageMonitor::unregisterAllSessions() {
    std::vector<SessionObserver*> sessions;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        sessions.swap(m_sessions);
        for (SessionObserver* observer : sessions) observer->detachOwner();
        m_sessionStates.clear();
        m_inactiveDeadlineTick = 0;
        syncSessionMetricsLocked();
    }

    for (SessionObserver* observer : sessions) {
        observer->unregisterCallback();
        observer->Release();
    }
}

bool MicUsageMonitor::init() {
    if (m_initialized.load(std::memory_order_acquire)) return true;
    m_stopping.store(false, std::memory_order_release);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        printf("MicUsageMonitor: COM init failed 0x%08lx\n", hr);
        return false;
    }
    m_comInitialized = true;

    m_wakeEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_wakeEvent) {
        printf("MicUsageMonitor: CreateEvent failed error=%lu\n",
            static_cast<unsigned long>(GetLastError()));
        shutdown();
        return false;
    }

    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&m_pEnumerator));
    if (FAILED(hr)) {
        printf("MicUsageMonitor: CoCreateInstance failed 0x%08lx\n", hr);
        shutdown();
        return false;
    }

    IMMDeviceCollection* captureCollection = nullptr;
    hr = m_pEnumerator->EnumAudioEndpoints(
        eCapture, DEVICE_STATE_ACTIVE, &captureCollection);
    if (FAILED(hr)) {
        printf("MicUsageMonitor: EnumAudioEndpoints(capture) failed 0x%08lx\n", hr);
        shutdown();
        return false;
    }

    UINT captureCount = 0;
    captureCollection->GetCount(&captureCount);
    for (UINT i = 0; i < captureCount && !m_pDevice; ++i) {
        IMMDevice* device = nullptr;
        if (FAILED(captureCollection->Item(i, &device))) continue;

        IPropertyStore* properties = nullptr;
        if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties))) {
            PROPVARIANT name;
            PropVariantInit(&name);
            if (SUCCEEDED(properties->GetValue(PKEY_Device_FriendlyName, &name)) &&
                name.vt == VT_LPWSTR && name.pwszVal &&
                wcsstr(name.pwszVal, L"CABLE Output") != nullptr) {
                printf("MicUsageMonitor: found CABLE Output capture: %ls\n",
                    name.pwszVal);
                m_pDevice = device;
            }
            PropVariantClear(&name);
            properties->Release();
        }
        if (!m_pDevice) device->Release();
    }
    captureCollection->Release();

    if (!m_pDevice) {
        printf("MicUsageMonitor: CABLE Output capture endpoint not found, falling back to default\n");
        hr = m_pEnumerator->GetDefaultAudioEndpoint(
            eCapture, eConsole, &m_pDevice);
        if (FAILED(hr)) {
            printf("MicUsageMonitor: GetDefaultAudioEndpoint failed 0x%08lx\n", hr);
            shutdown();
            return false;
        }
    }

    hr = m_pDevice->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL,
        nullptr, reinterpret_cast<void**>(&m_pSessionManager));
    if (FAILED(hr)) {
        printf("MicUsageMonitor: Activate IAudioSessionManager2 failed 0x%08lx\n", hr);
        shutdown();
        return false;
    }

    hr = m_pSessionManager->RegisterSessionNotification(this);
    if (FAILED(hr)) {
        printf("MicUsageMonitor: RegisterSessionNotification failed 0x%08lx\n", hr);
        shutdown();
        return false;
    }
    m_sessionNotificationRegistered = true;
    m_initialized.store(true, std::memory_order_release);

    // Register notifications before the first enumeration as required by the
    // Core Audio session notification contract.
    reconcile();
    printf("MicUsageMonitor: initialized (per-session events + reconciliation)\n");
    fflush(stdout);
    return true;
}

void MicUsageMonitor::reconcile() {
    if (!m_initialized.load(std::memory_order_acquire) ||
        m_stopping.load(std::memory_order_acquire)) {
        return;
    }

    IAudioSessionEnumerator* enumerator = nullptr;
    if (SUCCEEDED(m_pSessionManager->GetSessionEnumerator(&enumerator))) {
        int count = 0;
        enumerator->GetCount(&count);
        for (int i = 0; i < count; ++i) {
            IAudioSessionControl* session = nullptr;
            if (SUCCEEDED(enumerator->GetSession(i, &session))) {
                registerEventsOnSession(session);
                session->Release();
            }
        }
        enumerator->Release();
    }

    std::vector<SessionObserver*> snapshot;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        snapshot = m_sessions;
        for (SessionObserver* observer : snapshot) observer->AddRef();
    }
    for (SessionObserver* observer : snapshot) {
        observer->refreshState(PendingStateSource::Reconcile);
        processObserverState(observer);
        observer->Release();
    }

    cleanupExpiredSessions();
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        evaluateRequestedStateLocked("reconcile");
    }
}

void MicUsageMonitor::waitForChange(DWORD timeoutMs) {
    if (m_wakeEvent) {
        WaitForSingleObject(m_wakeEvent, timeoutMs);
    } else {
        Sleep(timeoutMs);
    }
}

void MicUsageMonitor::onDemandModeChanged() {
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        evaluateRequestedStateLocked("mode-changed");
    }
    if (m_wakeEvent) SetEvent(m_wakeEvent);
}

void MicUsageMonitor::shutdown() {
    if (!m_comInitialized && !m_pEnumerator && !m_pDevice &&
        !m_pSessionManager) {
        return;
    }

    m_stopping.store(true, std::memory_order_release);
    m_initialized.store(false, std::memory_order_release);

    if (m_pSessionManager && m_sessionNotificationRegistered) {
        m_pSessionManager->UnregisterSessionNotification(this);
        m_sessionNotificationRegistered = false;
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
    if (m_wakeEvent) {
        CloseHandle(m_wakeEvent);
        m_wakeEvent = nullptr;
    }
    if (m_comInitialized) {
        CoUninitialize();
        m_comInitialized = false;
    }
}
