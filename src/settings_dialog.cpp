#include "settings_dialog.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <commctrl.h>

#define SETTINGS_CLASS "AudioSourceSettingsClass"
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

struct SettingsInit {
    Config* pConfig;
    bool*   pOk;
};

struct SettingsDialogData {
    Config* pConfig;
    bool*   pOk;
    HWND hTab;
    std::vector<HWND> tabGeneralControls;
    std::vector<HWND> tabDspControls;
};

static void refreshDeviceList(HWND hCombo, const std::string& currentSerial) {
    SendMessageA(hCombo, CB_RESETCONTENT, 0, 0);
    int autoIdx = (int)SendMessageA(hCombo, CB_ADDSTRING, 0,
        (LPARAM)"Auto-detect (first available)");
    int selIdx = autoIdx;

    FILE* pipe = _popen("adb devices", "r");
    if (pipe) {
        char line[256];
        bool first = true;
        while (fgets(line, sizeof(line), pipe)) {
            if (first) { first = false; continue; }
            int len = (int)strlen(line);
            if (len == 0) continue;
            char* tab = strchr(line, '\t');
            if (!tab) continue;
            *tab = '\0';
            if (strstr(tab + 1, "device")) {
                while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == '\n'))
                    line[--len] = '\0';
                int idx = (int)SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)line);
                if (!currentSerial.empty() && currentSerial == std::string(line))
                    selIdx = idx;
            }
        }
        _pclose(pipe);
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

static void LoadConfigToUI(HWND hWnd, const Config* cfg) {
    // Gain
    int gainPos = (int)(cfg->gain * 100.0f);
    if (gainPos < 25) gainPos = 25;
    if (gainPos > 400) gainPos = 400;
    SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_GAIN), TBM_SETPOS, TRUE, gainPos);

    char gainText[32];
    snprintf(gainText, sizeof(gainText), "%.2fx", cfg->gain);
    SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_GAIN), gainText);

    // Hardware Algorithms
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NS), BM_SETCHECK,
        cfg->nsEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AEC), BM_SETCHECK,
        cfg->aecEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AGC), BM_SETCHECK,
        cfg->agcEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    // DSP
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
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA* pCreate = (CREATESTRUCTA*)lParam;
        SettingsInit* pInit = (SettingsInit*)pCreate->lpCreateParams;
        pData = new SettingsDialogData();
        pData->pConfig = pInit->pConfig;
        pData->pOk = pInit->pOk;
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)pData);

        HINSTANCE hInst = pCreate->hInstance;
        
        // Create Tab Control
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

        // --- General Tab Controls ---
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

        addGen(CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", pData->pConfig->host.c_str(),
            WS_CHILD | WS_VISIBLE,
            ctrlX, yBase + 1, 130, 21, hWnd, (HMENU)IDC_HOST_EDIT, hInst, NULL));

        yBase += 32;

        addGen(CreateWindowExA(0, "STATIC", "Port:",
            WS_CHILD | WS_VISIBLE,
            xMargin, yBase, lblW, 22, hWnd, NULL, hInst, NULL));

        std::string portStr = std::to_string(pData->pConfig->port);
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

        int appSel = pData->pConfig->androidAppPreset;
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

        // GroupBox for Android Hardware Algorithms
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


        // --- DSP Tab Controls ---
        yBase = 55;

        addDsp(CreateWindowExA(0, "BUTTON", "EQ Enable",
            WS_CHILD | BS_AUTOCHECKBOX, // Not visible initially
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

        // Load values into UI
        LoadConfigToUI(hWnd, pData->pConfig);

        // Buttons at the bottom
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

        refreshDeviceList(hCombo, pData->pConfig->serial);

        // Explicitly set font for all controls to match dialog default if possible
        // (Optional, omitted for simplicity unless requested)

        return 0;
    }

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
            LoadConfigToUI(hWnd, &defaultCfg);
            break;
        }
        case IDC_BTN_OK: {
            char buf[256];
            int idx = (int)SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
            if (idx <= 0) {
                pData->pConfig->serial = "";
            } else {
                SendMessageA(hCombo, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)buf);
                pData->pConfig->serial = buf;
            }
            GetWindowTextA(GetDlgItem(hWnd, IDC_HOST_EDIT), buf, sizeof(buf));
            pData->pConfig->host = buf;
            GetWindowTextA(GetDlgItem(hWnd, IDC_PORT_EDIT), buf, sizeof(buf));
            pData->pConfig->port = atoi(buf);
            if (pData->pConfig->port <= 0 || pData->pConfig->port > 65535)
                pData->pConfig->port = 27183;

            HWND hAppCombo = GetDlgItem(hWnd, IDC_COMBO_ANDROID_APP);
            int appIdx = (int)SendMessageA(hAppCombo, CB_GETCURSEL, 0, 0);
            pData->pConfig->androidAppPreset = appIdx;
            if (appIdx == 1) {
                pData->pConfig->androidSocket = "voxmicsource";
                pData->pConfig->androidComponent = "com.voxmic.source/.MainActivity";
            } else {
                pData->pConfig->androidSocket = "audiosource";
                pData->pConfig->androidComponent = "fr.dzx.audiosource/.MainActivity";
            }

            int gainPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_GAIN), TBM_GETPOS, 0, 0);
            pData->pConfig->gain = (float)gainPos / 100.0f;
            if (pData->pConfig->gain < 0.25f) pData->pConfig->gain = 0.25f;
            if (pData->pConfig->gain > 4.0f) pData->pConfig->gain = 4.0f;

            pData->pConfig->nsEnabled =
                (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NS), BM_GETCHECK, 0, 0) == BST_CHECKED);
            pData->pConfig->aecEnabled =
                (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AEC), BM_GETCHECK, 0, 0) == BST_CHECKED);
            pData->pConfig->agcEnabled =
                (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AGC), BM_GETCHECK, 0, 0) == BST_CHECKED);

            pData->pConfig->eqEnabled =
                (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_EQ), BM_GETCHECK, 0, 0) == BST_CHECKED);
            int presPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_PRES), TBM_GETPOS, 0, 0);
            pData->pConfig->eqPresence = (float)presPos / 10.0f;
            if (pData->pConfig->eqPresence < 0.0f) pData->pConfig->eqPresence = 0.0f;
            if (pData->pConfig->eqPresence > 6.0f) pData->pConfig->eqPresence = 6.0f;
            int bassPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_BASS), TBM_GETPOS, 0, 0);
            pData->pConfig->eqBassCut = -(float)bassPos / 10.0f;
            if (pData->pConfig->eqBassCut < -6.0f) pData->pConfig->eqBassCut = -6.0f;
            if (pData->pConfig->eqBassCut > 0.0f) pData->pConfig->eqBassCut = 0.0f;
            pData->pConfig->compressorEnabled =
                (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_COMP), BM_GETCHECK, 0, 0) == BST_CHECKED);
            pData->pConfig->nrEnabled =
                (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NR), BM_GETCHECK, 0, 0) == BST_CHECKED);

            pData->pConfig->save();
            *(pData->pOk) = true;
            DestroyWindow(hWnd);
            break;
        }
        case IDC_BTN_CANCEL:
            DestroyWindow(hWnd);
            break;
        }
        return 0;
    }

    case WM_DESTROY:
        delete pData;
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, 0);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

bool showSettingsDialog(HINSTANCE hInstance, HWND hParent, Config& config) {
    static bool classRegistered = false;
    static bool ccInitialized = false;
    if (!ccInitialized) {
        INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_TAB_CLASSES };
        InitCommonControlsEx(&icc);
        ccInitialized = true;
    }
    if (!classRegistered) {
        WNDCLASSEXA wc = {};
        wc.cbSize = sizeof(WNDCLASSEXA);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = hInstance;
        wc.lpszClassName = SETTINGS_CLASS;
        wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        RegisterClassExA(&wc);
        classRegistered = true;
    }

    Config tempConfig = config;
    bool ok = false;

    SettingsInit init = { &tempConfig, &ok };

    HWND hWnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        SETTINGS_CLASS,
        "AudioSource Win - Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        0, 0, 500, 460,
        hParent, NULL, hInstance, &init);

    if (!hWnd) return false;

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

    EnableWindow(hParent, FALSE);

    MSG msg;
    while (IsWindow(hWnd)) {
        BOOL bRet = GetMessageA(&msg, NULL, 0, 0);
        if (bRet <= 0) {
            if (bRet == 0)
                PostQuitMessage((int)msg.wParam);
            break;
        }
        
        // Handling tab key navigation if needed (optional for basic dialog)
        if (!IsDialogMessageA(hWnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    EnableWindow(hParent, TRUE);
    SetForegroundWindow(hParent);

    if (ok) {
        config = tempConfig;
        return true;
    }
    return false;
}
