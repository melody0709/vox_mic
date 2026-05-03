#include "tray_icon.h"
#include "resource.h"
#include "version.h"
#include <cstdio>

#pragma comment(lib, "shell32.lib")

TrayIcon::TrayIcon() {}

TrayIcon::~TrayIcon() {
    destroy();
}

bool TrayIcon::create(HINSTANCE hInstance, HWND hWnd) {
    m_hWnd = hWnd;

    m_nid.cbSize = sizeof(NOTIFYICONDATAA);
    m_nid.hWnd = hWnd;
    m_nid.uID = ID_TRAYICON;
    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    m_nid.uCallbackMessage = WM_TRAYICON;
    m_nid.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON_IDLE));
    strcpy_s(m_nid.szTip, "VoxMic - Idle");

    m_hMenu = CreatePopupMenu();
    AppendMenuA(m_hMenu, MF_STRING, ID_MENU_START, "Start Bridge");
    AppendMenuA(m_hMenu, MF_STRING, ID_MENU_STOP, "Stop Bridge");
    AppendMenuA(m_hMenu, MF_STRING, ID_MENU_DEMAND_MODE, "Demand Mode");
    AppendMenuA(m_hMenu, MF_STRING, ID_MENU_ALWAYS_HOT, "Always Hot");
    AppendMenuA(m_hMenu, MF_STRING, ID_MENU_SETTINGS, "Settings");
    AppendMenuA(m_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m_hMenu, MF_GRAYED, 0, APP_VERSION);
    AppendMenuA(m_hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m_hMenu, MF_STRING, ID_MENU_EXIT, "Exit");

    m_isCreated = Shell_NotifyIconA(NIM_ADD, &m_nid);
    return m_isCreated;
}

void TrayIcon::destroy() {
    if (m_isCreated) {
        Shell_NotifyIconA(NIM_DELETE, &m_nid);
        m_isCreated = false;
    }
    if (m_hMenu) {
        DestroyMenu(m_hMenu);
        m_hMenu = nullptr;
    }
}

void TrayIcon::updateIcon(bool isStreaming, bool isConnected) {
    if (!m_isCreated) return;

    if (isStreaming) {
        m_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_STREAMING));
        strcpy_s(m_nid.szTip, "VoxMic - Streaming");
    } else if (isConnected) {
        m_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_CONNECTED));
        strcpy_s(m_nid.szTip, "VoxMic - Connected");
    } else {
        m_nid.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON_IDLE));
        strcpy_s(m_nid.szTip, "VoxMic - Idle");
    }

    Shell_NotifyIconA(NIM_MODIFY, &m_nid);
}

void TrayIcon::showMenu(HWND hWnd) {
    if (!m_isCreated) return;

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hWnd);
    TrackPopupMenu(m_hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hWnd, NULL);
    PostMessage(hWnd, WM_NULL, 0, 0);
}

void TrayIcon::setDemandMode(bool on) {
    if (!m_isCreated || !m_hMenu) return;
    CheckMenuItem(m_hMenu, ID_MENU_DEMAND_MODE,
        on ? MF_CHECKED : MF_UNCHECKED);
}

void TrayIcon::setAlwaysHot(bool on) {
    if (!m_isCreated || !m_hMenu) return;
    CheckMenuItem(m_hMenu, ID_MENU_ALWAYS_HOT,
        on ? MF_CHECKED : MF_UNCHECKED);
}

void TrayIcon::showTooltip(const char* text) {
    if (!m_isCreated) return;

    strcpy_s(m_nid.szInfoTitle, "VoxMic");
    strcpy_s(m_nid.szInfo, text);
    m_nid.uFlags = NIF_INFO;
    m_nid.dwInfoFlags = NIIF_INFO;

    Shell_NotifyIconA(NIM_MODIFY, &m_nid);

    m_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}
