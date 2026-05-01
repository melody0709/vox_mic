#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <functional>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAYICON 1

#define ID_MENU_START     1001
#define ID_MENU_STOP      1002
#define ID_MENU_SETTINGS  1003
#define ID_MENU_EXIT      1004

class TrayIcon {
public:
    TrayIcon();
    ~TrayIcon();

    bool create(HINSTANCE hInstance, HWND hWnd);
    void destroy();
    void updateIcon(bool isStreaming, bool isConnected);
    void showMenu(HWND hWnd);
    void showTooltip(const char* text);

    std::function<void()> onStart;
    std::function<void()> onStop;
    std::function<void()> onExit;

private:
    NOTIFYICONDATAA m_nid{};
    HWND m_hWnd{nullptr};
    HMENU m_hMenu{nullptr};
    bool m_isCreated{false};
};
