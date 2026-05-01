#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

class MicUsageMonitor {
public:
    MicUsageMonitor();
    ~MicUsageMonitor();

    bool init();
    void shutdown();
    bool isCaptureActive();

private:
    void* m_pEnumerator;
    void* m_pDevice;
    bool  m_initialized;
};
