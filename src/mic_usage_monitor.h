#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "mic_session_state.h"

class MicUsageMonitor : public IAudioSessionNotification {
public:
    MicUsageMonitor();
    ~MicUsageMonitor();

    bool init();
    void shutdown();
    void reconcile();
    void waitForChange(DWORD timeoutMs);
    void onDemandModeChanged();

    int activeSessionCount() const {
        return m_activeSessionCount.load(std::memory_order_relaxed);
    }
    int trackedSessionCount() const {
        return m_trackedSessionCount.load(std::memory_order_relaxed);
    }
    uint64_t reconciliationCorrections() const {
        return m_reconciliationCorrections.load(std::memory_order_relaxed);
    }

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IAudioSessionNotification
    STDMETHOD(OnSessionCreated)(IAudioSessionControl* newSession) override;

private:
    class SessionObserver;
    friend class SessionObserver;

    void registerEventsOnSession(IAudioSessionControl* session);
    void unregisterAllSessions();
    void cleanupExpiredSessions();
    void notifyObserverStateChanged();
    void processObserverState(SessionObserver* observer);
    void evaluateRequestedStateLocked(const char* reason);
    void setMicRequestedLocked(bool requested, const char* reason);
    void syncSessionMetricsLocked();

    LONG m_refCount{1};
    IAudioSessionManager2* m_pSessionManager{nullptr};
    IMMDevice* m_pDevice{nullptr};
    IMMDeviceEnumerator* m_pEnumerator{nullptr};
    bool m_comInitialized{false};
    bool m_sessionNotificationRegistered{false};
    HANDLE m_wakeEvent{nullptr};
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_stopping{false};

    mutable std::mutex m_sessionsMutex;
    std::vector<SessionObserver*> m_sessions;
    MicSessionStateTracker m_sessionStates;
    uint64_t m_inactiveDeadlineTick{0};

    std::atomic<int> m_activeSessionCount{0};
    std::atomic<int> m_trackedSessionCount{0};
    std::atomic<uint64_t> m_reconciliationCorrections{0};
};
