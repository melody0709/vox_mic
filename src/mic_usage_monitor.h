#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <atomic>
#include <vector>
#include <mutex>

class MicUsageMonitor
    : public IAudioSessionNotification
    , public IAudioSessionEvents
{
public:
    MicUsageMonitor();
    ~MicUsageMonitor();

    bool init();
    void shutdown();
    float getCapturePeak();

    // IUnknown
    STDMETHOD(QueryInterface)(REFIID riid, void** ppv) override;
    STDMETHOD_(ULONG, AddRef)() override;
    STDMETHOD_(ULONG, Release)() override;

    // IAudioSessionNotification
    STDMETHOD(OnSessionCreated)(IAudioSessionControl *NewSession) override;

    // IAudioSessionEvents
    STDMETHOD(OnDisplayNameChanged)(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHOD(OnIconPathChanged)(LPCWSTR, LPCGUID) override { return S_OK; }
    STDMETHOD(OnSimpleVolumeChanged)(float, BOOL, LPCGUID) override { return S_OK; }
    STDMETHOD(OnChannelVolumeChanged)(DWORD, float*, DWORD, LPCGUID) override { return S_OK; }
    STDMETHOD(OnGroupingParamChanged)(LPCGUID, LPCGUID) override { return S_OK; }
    STDMETHOD(OnStateChanged)(AudioSessionState NewState) override;
    STDMETHOD(OnSessionDisconnected)(AudioSessionDisconnectReason DisconnectReason) override;

private:
    void registerEventsOnSession(IAudioSessionControl* pSession);
    void unregisterAllSessions();

    LONG m_refCount{1};
    IAudioSessionManager2* m_pSessionManager{nullptr};
    IMMDevice* m_pDevice{nullptr};
    IMMDeviceEnumerator* m_pEnumerator{nullptr};
    IAudioMeterInformation* m_pMeter{nullptr};
    bool m_initialized{false};
    std::atomic<bool> m_stopping{false};

    std::atomic<int> m_activeCount{0};
    std::mutex m_sessionsMutex;
    std::vector<IAudioSessionControl*> m_registeredSessions;
};
