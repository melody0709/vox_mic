#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAYICON 1

#define ID_MENU_DEMAND_MODE  1005
#define ID_MENU_ALWAYS_HOT   1006
#define ID_MENU_SETTINGS     1003
#define ID_MENU_EXIT         1004

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    bool create(HINSTANCE hInstance, HWND hWnd);
    void destroy();
    bool handleWindowMessage(UINT message);
    void updateIcon(bool isStreaming, bool isConnected);
    void showMenu(HWND hWnd);
    void showTooltip(const char* text);
    void setDemandMode(bool on);
    void setAlwaysHot(bool on);

    std::function<void()> onExit;

private:
    NOTIFYICONDATAA m_nid{};
    HWND m_hWnd{nullptr};
    HMENU m_hMenu{nullptr};
    UINT m_taskbarCreatedMessage{0};
    bool m_isCreated{false};

    bool addIcon();
};
