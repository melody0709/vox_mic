#include "settings_dialog.h"
#include "adb_control.h"
#include "tray_icon.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <commctrl.h>
#include <atomic>

extern Config g_config;
extern std::atomic<bool> g_running;
extern std::atomic<bool> g_bridgeActive;
extern std::atomic<bool> g_demandMode;
extern TrayIcon* g_trayIcon;
extern void syncDspAtomsFromConfig();

#define SETTINGS_CLASS "VoxMicSettingsClass"
#define IDC_COMBO_DEVICE      2001
#define IDC_HOST_EDIT         2002
#define IDC_PORT_EDIT         2003
#define IDC_BTN_REFRESH       2004
#define IDC_BTN_OK            2005
#define IDC_BTN_CANCEL        2006
#define IDC_COMBO_ANDROID_APP 2007
#define IDC_TRACKBAR_GAIN     2008
#define IDC_LABEL_GAIN        2009
#define IDC_CHECK_NS          2010
#define IDC_CHECK_AEC         2011
#define IDC_CHECK_AGC         2012
#define IDC_CHECK_EQ          2013
#define IDC_TRACKBAR_PRES     2014
#define IDC_LABEL_PRES        2015
#define IDC_TRACKBAR_BASS     2016
#define IDC_LABEL_BASS        2017
#define IDC_CHECK_COMP        2018
#define IDC_CHECK_NR          2019
#define IDC_TAB_MAIN          2020
#define IDC_BTN_RESET         2021
#define IDC_CHECK_DEBUG       2022

struct SettingsDialogData {
    Config* pConfig;
    HWND hTab;
    std::vector<HWND> tabGeneralControls;
    std::vector<HWND> tabDspControls;
};

static void refreshDeviceList(HWND hCombo, const std::string& currentSerial) {
    SendMessageA(hCombo, CB_RESETCONTENT, 0, 0);
    int autoIdx = (int)SendMessageA(hCombo, CB_ADDSTRING, 0,
        (LPARAM)"Auto-detect (first available)");
    int selIdx = autoIdx;

    std::string result = runCommandNoWindow("adb devices");
    if (!result.empty()) {
        std::string line;
        bool first = true;
        for (size_t i = 0, start = 0; i <= result.size(); i++) {
            if (i == result.size() || result[i] == '\n') {
                line = result.substr(start, i - start);
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                start = i + 1;

                if (first) { first = false; continue; }
                if (line.empty()) continue;
                size_t tab = line.find('\t');
                if (tab == std::string::npos) continue;
                std::string serial = line.substr(0, tab);
                std::string rest = line.substr(tab + 1);
                if (rest.find("device") != std::string::npos) {
                    int idx = (int)SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)serial.c_str());
                    if (!currentSerial.empty() && currentSerial == serial)
                        selIdx = idx;
                }
            }
        }
    } else {
        SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)"(ADB not available)");
    }

    SendMessageA(hCombo, CB_SETCURSEL, (WPARAM)selIdx, 0);
}

static void updatePresLabel(HWND hWnd) {
    int pos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_PRES), TBM_GETPOS, 0, 0);
    float val = (float)pos / 10.0f;
    char buf[32];
    snprintf(buf, sizeof(buf), "+%.1f dB", val);
    SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_PRES), buf);
}

static void updateBassLabel(HWND hWnd) {
    int pos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_BASS), TBM_GETPOS, 0, 0);
    float val = -(float)pos / 10.0f;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f dB", val);
    SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_BASS), buf);
}

static void showTabControls(const SettingsDialogData* pData, int tabIndex) {
    int swGen = (tabIndex == 0) ? SW_SHOW : SW_HIDE;
    int swDsp = (tabIndex == 1) ? SW_SHOW : SW_HIDE;
    for (HWND h : pData->tabGeneralControls) {
        ShowWindow(h, swGen);
    }
    for (HWND h : pData->tabDspControls) {
        ShowWindow(h, swDsp);
    }
}

static void loadUiFromConfig(HWND hWnd, const Config* cfg) {
    int gainPos = (int)(cfg->gain * 100.0f);
    if (gainPos < 25) gainPos = 25;
    if (gainPos > 400) gainPos = 400;
    SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_GAIN), TBM_SETPOS, TRUE, gainPos);

    char gainText[32];
    snprintf(gainText, sizeof(gainText), "%.2fx", cfg->gain);
    SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_GAIN), gainText);

    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NS), BM_SETCHECK,
        cfg->nsEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AEC), BM_SETCHECK,
        cfg->aecEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AGC), BM_SETCHECK,
        cfg->agcEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_EQ), BM_SETCHECK,
        cfg->eqEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    int presPos = (int)(cfg->eqPresence * 10.0f);
    if (presPos < 0) presPos = 0;
    if (presPos > 60) presPos = 60;
    SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_PRES), TBM_SETPOS, TRUE, presPos);
    updatePresLabel(hWnd);

    int bassPos = (int)(-cfg->eqBassCut * 10.0f);
    if (bassPos < 0) bassPos = 0;
    if (bassPos > 60) bassPos = 60;
    SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_BASS), TBM_SETPOS, TRUE, bassPos);
    updateBassLabel(hWnd);

    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_COMP), BM_SETCHECK,
        cfg->compressorEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NR), BM_SETCHECK,
        cfg->nrEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_DEBUG), BM_SETCHECK,
        cfg->debugConsole ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void saveUiToConfig(HWND hWnd, Config* cfg) {
    char buf[256];
    int idx = (int)SendMessageA(GetDlgItem(hWnd, IDC_COMBO_DEVICE), CB_GETCURSEL, 0, 0);
    if (idx <= 0) {
        cfg->serial = "";
    } else {
        SendMessageA(GetDlgItem(hWnd, IDC_COMBO_DEVICE), CB_GETLBTEXT, (WPARAM)idx, (LPARAM)buf);
        cfg->serial = buf;
    }
    GetWindowTextA(GetDlgItem(hWnd, IDC_HOST_EDIT), buf, sizeof(buf));
    cfg->host = buf;
    GetWindowTextA(GetDlgItem(hWnd, IDC_PORT_EDIT), buf, sizeof(buf));
    cfg->port = atoi(buf);
    if (cfg->port <= 0 || cfg->port > 65535)
        cfg->port = 27183;

    HWND hAppCombo = GetDlgItem(hWnd, IDC_COMBO_ANDROID_APP);
    int appIdx = (int)SendMessageA(hAppCombo, CB_GETCURSEL, 0, 0);
    cfg->androidAppPreset = appIdx;
    if (appIdx == 1) {
        cfg->androidSocket = "voxmicsource";
        cfg->androidComponent = "com.voxmic.source/.MainActivity";
    } else {
        cfg->androidSocket = "audiosource";
        cfg->androidComponent = "fr.dzx.audiosource/.MainActivity";
    }

    int gainPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_GAIN), TBM_GETPOS, 0, 0);
    cfg->gain = (float)gainPos / 100.0f;
    if (cfg->gain < 0.25f) cfg->gain = 0.25f;
    if (cfg->gain > 4.0f) cfg->gain = 4.0f;

    cfg->nsEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NS), BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg->aecEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AEC), BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg->agcEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AGC), BM_GETCHECK, 0, 0) == BST_CHECKED);

    cfg->eqEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_EQ), BM_GETCHECK, 0, 0) == BST_CHECKED);
    int presPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_PRES), TBM_GETPOS, 0, 0);
    cfg->eqPresence = (float)presPos / 10.0f;
    if (cfg->eqPresence < 0.0f) cfg->eqPresence = 0.0f;
    if (cfg->eqPresence > 6.0f) cfg->eqPresence = 6.0f;
    int bassPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_BASS), TBM_GETPOS, 0, 0);
    cfg->eqBassCut = -(float)bassPos / 10.0f;
    if (cfg->eqBassCut < -6.0f) cfg->eqBassCut = -6.0f;
    if (cfg->eqBassCut > 0.0f) cfg->eqBassCut = 0.0f;
    cfg->compressorEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_COMP), BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg->nrEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NR), BM_GETCHECK, 0, 0) == BST_CHECKED);

    cfg->debugConsole =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_DEBUG), BM_GETCHECK, 0, 0) == BST_CHECKED);
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA* pCreate = (CREATESTRUCTA*)lParam;
        Config* cfg = (Config*)pCreate->lpCreateParams;
        pData = new SettingsDialogData();
        pData->pConfig = cfg;
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)pData);

        HINSTANCE hInst = pCreate->hInstance;
        
        pData->hTab = CreateWindowExA(0, WC_TABCONTROLA, "",
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
            10, 10, 465, 360, hWnd, (HMENU)IDC_TAB_MAIN, hInst, NULL);
            
        TCITEMA tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPSTR)"General";
        SendMessageA(pData->hTab, TCM_INSERTITEMA, 0, (LPARAM)&tie);
        tie.pszText = (LPSTR)"DSP";
        SendMessageA(pData->hTab, TCM_INSERTITEMA, 1, (LPARAM)&tie);

        int xMargin = 25;
        int yBase = 45;
        int lblW = 100;
        int ctrlX = xMargin + lblW + 8;
        
        auto addGen = [&](HWND h) { pData->tabGeneralControls.push_back(h); return h; };
        auto addDsp = [&](HWND h) { pData->tabDspControls.push_back(h); return h; };

        addGen(CreateWindowExA(0, "STATIC", "ADB Device:",
            WS_CHILD | WS_VISIBLE,
            xMargin, yBase, lblW, 22, hWnd, NULL, hInst, NULL));

        HWND hCombo = addGen(CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            ctrlX, yBase, 195, 200, hWnd, (HMENU)IDC_COMBO_DEVICE, hInst, NULL));

        addGen(CreateWindowExA(0, "BUTTON", "Refresh",
            WS_CHILD | WS_VISIBLE,
            ctrlX + 205, yBase - 1, 55, 24,
            hWnd, (HMENU)IDC_BTN_REFRESH, hInst, NULL));

        yBase += 32;

        addGen(CreateWindowExA(0, "STATIC", "Host:",
            WS_CHILD | WS_VISIBLE,
            xMargin, yBase, lblW, 22, hWnd, NULL, hInst, NULL));

        addGen(CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cfg->host.c_str(),
            WS_CHILD | WS_VISIBLE,
            ctrlX, yBase + 1, 130, 21, hWnd, (HMENU)IDC_HOST_EDIT, hInst, NULL));

        yBase += 32;

        addGen(CreateWindowExA(0, "STATIC", "Port:",
            WS_CHILD | WS_VISIBLE,
            xMargin, yBase, lblW, 22, hWnd, NULL, hInst, NULL));

        std::string portStr = std::to_string(cfg->port);
        addGen(CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", portStr.c_str(),
            WS_CHILD | WS_VISIBLE,
            ctrlX, yBase + 1, 90, 21, hWnd, (HMENU)IDC_PORT_EDIT, hInst, NULL));

        yBase += 32;

        addGen(CreateWindowExA(0, "STATIC", "Android App:",
            WS_CHILD | WS_VISIBLE,
            xMargin, yBase, lblW, 22, hWnd, NULL, hInst, NULL));

        HWND hAppCombo = addGen(CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            ctrlX, yBase, 250, 200, hWnd, (HMENU)IDC_COMBO_ANDROID_APP, hInst, NULL));

        SendMessageA(hAppCombo, CB_ADDSTRING, 0, (LPARAM)"Original AudioSource (gdzx) - DEFAULT / no effects");
        SendMessageA(hAppCombo, CB_ADDSTRING, 0, (LPARAM)"VoxMic Source (improved) - 48000Hz / DEFAULT + NS + AEC");

        int appSel = cfg->androidAppPreset;
        if (appSel < 0 || appSel > 1) appSel = 0;
        SendMessageA(hAppCombo, CB_SETCURSEL, (WPARAM)appSel, 0);

        yBase += 32;

        addGen(CreateWindowExA(0, "STATIC", "Gain:",
            WS_CHILD | WS_VISIBLE,
            xMargin, yBase, lblW, 22, hWnd, NULL, hInst, NULL));

        HWND hGainLabel = addGen(CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE,
            ctrlX + 195, yBase, 70, 22, hWnd, (HMENU)IDC_LABEL_GAIN, hInst, NULL));

        HWND hGainTrackbar = addGen(CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_TOOLTIPS,
            ctrlX, yBase + 1, 190, 24, hWnd, (HMENU)IDC_TRACKBAR_GAIN, hInst, NULL));
        SendMessageA(hGainTrackbar, TBM_SETRANGE, TRUE, MAKELONG(25, 400));
        SendMessageA(hGainTrackbar, TBM_SETTICFREQ, 25, 0);

        yBase += 38;

        addGen(CreateWindowExA(0, "BUTTON", "Android Hardware Algorithms",
            WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
            xMargin, yBase, 420, 95, hWnd, NULL, hInst, NULL));
            
        int grpX = xMargin + 15;
        int grpY = yBase + 25;

        addGen(CreateWindowExA(0, "BUTTON", "NoiseSuppressor",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            grpX, grpY, 150, 22, hWnd, (HMENU)IDC_CHECK_NS, hInst, NULL));

        addGen(CreateWindowExA(0, "BUTTON", "AcousticEchoCanceler",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            grpX + 170, grpY, 180, 22, hWnd, (HMENU)IDC_CHECK_AEC, hInst, NULL));

        grpY += 30;

        addGen(CreateWindowExA(0, "BUTTON", "AutomaticGainControl",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            grpX, grpY, 160, 22, hWnd, (HMENU)IDC_CHECK_AGC, hInst, NULL));

        yBase += 95 + 12;

        addGen(CreateWindowExA(0, "BUTTON", "Debug Console",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            xMargin, yBase, 200, 22, hWnd, (HMENU)IDC_CHECK_DEBUG, hInst, NULL));

        // --- DSP Tab Controls ---
        yBase = 55;

        addDsp(CreateWindowExA(0, "BUTTON", "EQ Enable",
            WS_CHILD | BS_AUTOCHECKBOX,
            xMargin, yBase, 100, 22, hWnd, (HMENU)IDC_CHECK_EQ, hInst, NULL));

        yBase += 35;

        addDsp(CreateWindowExA(0, "STATIC", "Presence:",
            WS_CHILD,
            xMargin + 10, yBase, lblW - 10, 22, hWnd, NULL, hInst, NULL));

        addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD,
            ctrlX + 195, yBase, 70, 22, hWnd, (HMENU)IDC_LABEL_PRES, hInst, NULL));

        HWND hPresTrackbar = addDsp(CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | TBS_HORZ | TBS_TOOLTIPS,
            ctrlX, yBase + 1, 190, 24, hWnd, (HMENU)IDC_TRACKBAR_PRES, hInst, NULL));
        SendMessageA(hPresTrackbar, TBM_SETRANGE, TRUE, MAKELONG(0, 60));
        SendMessageA(hPresTrackbar, TBM_SETTICFREQ, 10, 0);

        yBase += 35;

        addDsp(CreateWindowExA(0, "STATIC", "Bass Cut:",
            WS_CHILD,
            xMargin + 10, yBase, lblW - 10, 22, hWnd, NULL, hInst, NULL));

        addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD,
            ctrlX + 195, yBase, 70, 22, hWnd, (HMENU)IDC_LABEL_BASS, hInst, NULL));

        HWND hBassTrackbar = addDsp(CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | TBS_HORZ | TBS_TOOLTIPS,
            ctrlX, yBase + 1, 190, 24, hWnd, (HMENU)IDC_TRACKBAR_BASS, hInst, NULL));
        SendMessageA(hBassTrackbar, TBM_SETRANGE, TRUE, MAKELONG(0, 60));
        SendMessageA(hBassTrackbar, TBM_SETTICFREQ, 10, 0);

        yBase += 40;

        addDsp(CreateWindowExA(0, "BUTTON", "Compressor Enable",
            WS_CHILD | BS_AUTOCHECKBOX,
            xMargin, yBase, 150, 22, hWnd, (HMENU)IDC_CHECK_COMP, hInst, NULL));

        yBase += 35;

        addDsp(CreateWindowExA(0, "BUTTON", "Noise Reduction",
            WS_CHILD | BS_AUTOCHECKBOX,
            xMargin, yBase, 150, 22, hWnd, (HMENU)IDC_CHECK_NR, hInst, NULL));

        loadUiFromConfig(hWnd, cfg);

        int btnY = 385;
        
        CreateWindowExA(0, "BUTTON", "Reset to Defaults",
            WS_CHILD | WS_VISIBLE,
            10, btnY, 120, 25, hWnd, (HMENU)IDC_BTN_RESET, hInst, NULL);
        CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            155, btnY, 75, 25, hWnd, (HMENU)IDC_BTN_OK, hInst, NULL);

        CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE,
            245, btnY, 75, 25, hWnd, (HMENU)IDC_BTN_CANCEL, hInst, NULL);

        refreshDeviceList(hCombo, cfg->serial);

        return 0;
    }

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            if (g_trayIcon) g_trayIcon->showMenu(hWnd);
        }
        return 0;

    case WM_NOTIFY: {
        LPNMHDR lpnmhdr = (LPNMHDR)lParam;
        if (lpnmhdr->idFrom == IDC_TAB_MAIN && lpnmhdr->code == TCN_SELCHANGE) {
            int sel = (int)SendMessageA(pData->hTab, TCM_GETCURSEL, 0, 0);
            showTabControls(pData, sel);
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND hTrackbar = (HWND)lParam;
        if (hTrackbar == GetDlgItem(hWnd, IDC_TRACKBAR_GAIN)) {
            int pos = (int)SendMessageA(hTrackbar, TBM_GETPOS, 0, 0);
            float g = (float)pos / 100.0f;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2fx", g);
            SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_GAIN), buf);
        } else if (hTrackbar == GetDlgItem(hWnd, IDC_TRACKBAR_PRES)) {
            updatePresLabel(hWnd);
        } else if (hTrackbar == GetDlgItem(hWnd, IDC_TRACKBAR_BASS)) {
            updateBassLabel(hWnd);
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        HWND hCombo = GetDlgItem(hWnd, IDC_COMBO_DEVICE);
        switch (id) {
        case IDC_BTN_REFRESH:
            refreshDeviceList(hCombo, pData->pConfig->serial);
            break;
        case IDC_BTN_RESET: {
            Config defaultCfg;
            loadUiFromConfig(hWnd, &defaultCfg);
            break;
        }
        case IDC_BTN_OK:
            saveUiToConfig(hWnd, pData->pConfig);
            pData->pConfig->save();
            syncDspAtomsFromConfig();
            ShowWindow(hWnd, SW_HIDE);
            break;
        case IDC_BTN_CANCEL:
            loadUiFromConfig(hWnd, pData->pConfig);
            ShowWindow(hWnd, SW_HIDE);
            break;

        case ID_MENU_START:
            g_bridgeActive.store(true);
            break;
        case ID_MENU_STOP:
            g_bridgeActive.store(false);
            break;
        case ID_MENU_DEMAND_MODE: {
            bool newVal = !g_demandMode.load();
            g_demandMode.store(newVal);
            if (g_trayIcon) g_trayIcon->setDemandMode(newVal);
            g_config.demandMode = newVal;
            g_config.save();
            if (newVal)
                printf("[Demand] Mode ON (mic monitor active)\n");
            else
                printf("[Demand] Mode OFF (always stream)\n");
            fflush(stdout);
            break;
        }
        case ID_MENU_SETTINGS:
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
            break;
        case ID_MENU_EXIT:
            if (g_trayIcon) g_trayIcon->destroy();
            g_running.store(false);
            PostQuitMessage(0);
            break;
        }
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hWnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        delete pData;
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

void loadSettingsWindow(HWND hWnd, const Config* cfg) {
    loadUiFromConfig(hWnd, cfg);
}

HWND createSettingsWindow(HINSTANCE hInstance, Config* pConfig) {
    static bool ccInitialized = false;
    if (!ccInitialized) {
        INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_TAB_CLASSES };
        InitCommonControlsEx(&icc);
        ccInitialized = true;
    }

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = SETTINGS_CLASS;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        SETTINGS_CLASS,
        "VoxMic - Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 500, 460,
        NULL, NULL, hInstance, pConfig);

    if (!hWnd) return NULL;

    RECT childRect;
    GetWindowRect(hWnd, &childRect);
    int childW = childRect.right - childRect.left;
    int childH = childRect.bottom - childRect.top;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(hWnd, NULL,
        (screenW - childW) / 2,
        (screenH - childH) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    return hWnd;
}
