#include "settings_dialog.h"
#include "adb_control.h"
#include "tray_icon.h"
#include "startup_registration.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <commctrl.h>
#include <atomic>

extern Config g_config;
extern std::atomic<bool> g_running;
extern std::atomic<bool> g_demandMode;
extern std::atomic<bool> g_alwaysHot;
extern std::atomic<bool> g_dpdfnetAvailable;
extern std::atomic<bool> g_dpdfnetDegraded;
extern std::atomic<bool> g_nrEnabled;
extern std::atomic<int> g_denoiseBackend;
extern std::atomic<int> g_denoiseEffectiveBackend;
extern TrayIcon* g_trayIcon;
extern void syncDspAtomsFromConfig(const Config& cfg);
extern void requestDenoiseReset();
extern void setDemandModeRuntime(bool enabled);

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
#define IDC_TRACKBAR_NRSTR    2023
#define IDC_LABEL_NRSTR       2024
#define IDC_CHECK_STARTUP     2025
#define IDC_LABEL_STARTUP_HINT 2026
#define IDC_COMBO_NR_BACKEND  2027
#define IDC_LABEL_NR_BACKEND_STATUS 2028
#define IDC_BTN_APPLY         2029
#define IDC_LABEL_DSP_CHAIN_STATUS 2031
#define IDC_LABEL_COMP_HINT   2032
#define ID_TIMER_BACKEND_STATUS 2

struct SettingsDialogData {
    Config* pConfig;
    Config editBaseConfig;
    bool hasEditBase = false;
    bool dirty = false;
    HWND hTab;
    HWND hApply = nullptr;
    HWND hDeviceCombo = nullptr;
    HWND hAndroidAppCombo = nullptr;
    HWND hNrBackendCombo = nullptr;
    HWND hDspChainStatus = nullptr;
    HWND hNrStrengthHint = nullptr;
    std::vector<HWND> tabGeneralControls;
    std::vector<HWND> tabDspControls;
    std::vector<HWND> hintControls;
    std::vector<HWND> eqDependentControls;
    std::vector<HWND> nrDependentControls;
    HFONT hHintFont;
    HFONT hSectionFont;
    HBRUSH hInputBrush = nullptr;
    bool useSystemInputColors = false;
};

static COLORREF inputBackgroundColor(const SettingsDialogData* pData) {
    return pData && pData->useSystemInputColors
        ? GetSysColor(COLOR_WINDOW)
        : RGB(255, 255, 255);
}

static COLORREF inputTextColor(const SettingsDialogData* pData) {
    return pData && pData->useSystemInputColors
        ? GetSysColor(COLOR_WINDOWTEXT)
        : RGB(32, 32, 32);
}

static HBRUSH inputBackgroundBrush(const SettingsDialogData* pData) {
    if (pData && pData->useSystemInputColors) {
        return GetSysColorBrush(COLOR_WINDOW);
    }
    if (pData && pData->hInputBrush) return pData->hInputBrush;
    return (HBRUSH)GetStockObject(WHITE_BRUSH);
}

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

static void selectDeviceInList(HWND hCombo, const std::string& currentSerial) {
    if (!hCombo) return;
    int selection = 0;
    if (!currentSerial.empty()) {
        selection = (int)SendMessageA(
            hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)currentSerial.c_str());
        if (selection < 0) selection = 0;
    }
    SendMessageA(hCombo, CB_SETCURSEL, (WPARAM)selection, 0);
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

static void updateNrStrLabel(HWND hWnd) {
    int pos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_NRSTR), TBM_GETPOS, 0, 0);
    float val = (float)pos / 100.0f;
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", val);
    SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_NRSTR), buf);
}

static bool isChecked(HWND hWnd, int controlId) {
    return SendMessageA(GetDlgItem(hWnd, controlId), BM_GETCHECK, 0, 0) == BST_CHECKED;
}

static void setControlEnabledIfChanged(HWND hControl, bool enabled) {
    if (!hControl) return;
    const BOOL desired = enabled ? TRUE : FALSE;
    if (IsWindowEnabled(hControl) != desired) {
        EnableWindow(hControl, desired);
    }
}

static void setControlTextIfChanged(HWND hControl, const char* text) {
    if (!hControl || !text) return;

    char current[512] = {};
    GetWindowTextA(hControl, current, static_cast<int>(sizeof(current)));
    if (std::strcmp(current, text) != 0) {
        SetWindowTextA(hControl, text);
    }
}

static void updateDirtyUi(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData) return;

    if (pData->hApply) {
        setControlEnabledIfChanged(pData->hApply, pData->dirty);
    }
    setControlTextIfChanged(hWnd, pData->dirty
        ? "VoxMic - Settings *"
        : "VoxMic - Settings");
}

static void setSettingsDirty(HWND hWnd, bool dirty) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData) return;
    pData->dirty = dirty;
    updateDirtyUi(hWnd);
}

static void markSettingsDirty(HWND hWnd) {
    setSettingsDirty(hWnd, true);
}

static void updateDspControlStates(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData) return;

    const bool eqEnabled = isChecked(hWnd, IDC_CHECK_EQ);
    for (HWND control : pData->eqDependentControls) {
        setControlEnabledIfChanged(control, eqEnabled);
    }

    const bool nrEnabled = isChecked(hWnd, IDC_CHECK_NR);
    for (HWND control : pData->nrDependentControls) {
        setControlEnabledIfChanged(control, nrEnabled);
    }

    HWND backendCombo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    const int selection = backendCombo
        ? (int)SendMessageA(backendCombo, CB_GETCURSEL, 0, 0)
        : 0;
    const bool nrStrengthEnabled = nrEnabled && selection != 1;
    setControlEnabledIfChanged(
        GetDlgItem(hWnd, IDC_TRACKBAR_NRSTR), nrStrengthEnabled);
    setControlEnabledIfChanged(
        GetDlgItem(hWnd, IDC_LABEL_NRSTR), nrStrengthEnabled);

    if (pData->hNrStrengthHint) {
        const char* hint = !nrEnabled
            ? "Enable Noise Reduction to adjust these controls."
            : (selection == 1
                ? "Strength applies to RNNoise only."
                : "Lower = gentler, keeps natural tone  |  Higher = stronger noise suppression");
        setControlTextIfChanged(pData->hNrStrengthHint, hint);
    }
}

static void updateProcessingChainUi(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData || !pData->hDspChainStatus) return;

    const bool nrEnabled = isChecked(hWnd, IDC_CHECK_NR);
    const bool eqEnabled = isChecked(hWnd, IDC_CHECK_EQ);
    const bool compressorEnabled = isChecked(hWnd, IDC_CHECK_COMP);
    HWND backendCombo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    const int selectedBackend = backendCombo
        ? (int)SendMessageA(backendCombo, CB_GETCURSEL, 0, 0)
        : 0;

    const char* denoiseName = "Off";
    if (nrEnabled) {
        if (selectedBackend == 1) {
            const bool fallback =
                g_dpdfnetDegraded.load(std::memory_order_acquire) ||
                !g_dpdfnetAvailable.load(std::memory_order_acquire);
            if (fallback) {
                denoiseName = "RNNoise fallback";
            } else if (g_denoiseEffectiveBackend.load(
                           std::memory_order_acquire) == 1) {
                denoiseName = "DPDFNet";
            } else {
                denoiseName = "DPDFNet pending";
            }
        } else {
            denoiseName = "RNNoise";
        }
    }

    char chainText[256];
    snprintf(chainText, sizeof(chainText),
        "Path: NR[%s] > EQ[%s] > Comp[%s] > Limiter[On]",
        denoiseName,
        eqEnabled ? "On" : "Off",
        compressorEnabled ? "On" : "Off");

    char previousText[256] = {};
    GetWindowTextA(pData->hDspChainStatus, previousText,
        static_cast<int>(sizeof(previousText)));
    if (std::strcmp(previousText, chainText) != 0) {
        SetWindowTextA(pData->hDspChainStatus, chainText);
        if (IsWindowVisible(pData->hDspChainStatus)) {
            RedrawWindow(pData->hDspChainStatus, nullptr, nullptr,
                RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
        }
    }
}

static COLORREF denoiseStatusColor(HWND hWnd) {
    if (!isChecked(hWnd, IDC_CHECK_NR)) return RGB(128, 128, 128);

    HWND combo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    const int selection = combo
        ? (int)SendMessageA(combo, CB_GETCURSEL, 0, 0)
        : 0;
    if (selection == 1) {
        if (g_dpdfnetDegraded.load(std::memory_order_acquire) ||
            !g_dpdfnetAvailable.load(std::memory_order_acquire)) {
            return RGB(168, 104, 24);
        }
        if (g_denoiseEffectiveBackend.load(std::memory_order_acquire) == 1) {
            return RGB(30, 120, 54);
        }
        return RGB(45, 92, 150);
    }

    if (g_denoiseEffectiveBackend.load(std::memory_order_acquire) == 0) {
        return RGB(30, 120, 54);
    }
    return RGB(45, 92, 150);
}

static void updateDenoiseBackendUi(HWND hWnd) {
    HWND combo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    HWND status = GetDlgItem(hWnd, IDC_LABEL_NR_BACKEND_STATUS);
    if (!combo || !status) return;

    const int selection = (int)SendMessageA(combo, CB_GETCURSEL, 0, 0);
    const bool dpdfnetRequested = selection == 1;
    const bool nrEnabled = g_nrEnabled.load(std::memory_order_acquire);
    updateDspControlStates(hWnd);

    const int activeRequestedBackend = g_denoiseBackend.load(
        std::memory_order_acquire);
    const bool selectionIsApplied = activeRequestedBackend ==
        (dpdfnetRequested ? 1 : 0);
    const char* statusText = nullptr;
    if (!nrEnabled) {
        statusText = "Noise reduction is disabled; enable it to use the selected backend.";
    } else if (!dpdfnetRequested) {
        statusText = selectionIsApplied
            ? (g_denoiseEffectiveBackend.load(std::memory_order_acquire) == 0
                ? "RNNoise is active."
                : "RNNoise is selected; it will take effect at the next audio block.")
            : "RNNoise is selected; it will take effect at the next audio block.";
    } else if (!selectionIsApplied) {
        statusText = g_dpdfnetAvailable.load(std::memory_order_acquire)
            ? "DPDFNet is ready; it will take effect at the next audio block."
            : "DPDFNet is unavailable; audio will use RNNoise fallback.";
    } else if (g_dpdfnetDegraded.load(std::memory_order_acquire)) {
        statusText =
            "DPDFNet was degraded to RNNoise after a worker stall; it will retry after the next stream reset.";
    } else if (!g_dpdfnetAvailable.load(std::memory_order_acquire)) {
        statusText =
            "DPDFNet is unavailable; audio will use RNNoise until the runtime/model/session is available.";
    } else if (g_denoiseEffectiveBackend.load(std::memory_order_acquire) == 1) {
        statusText = "DPDFNet is ready and selected.";
    } else {
        statusText = "DPDFNet is ready; it will take effect at the next audio block.";
    }

    char previousText[512] = {};
    GetWindowTextA(status, previousText, static_cast<int>(sizeof(previousText)));
    if (std::strcmp(previousText, statusText) != 0) {
        // The status text changes while the window remains open. Clear first
        // and force an immediate repaint so a shorter replacement cannot
        // leave glyphs from the previous message behind.
        SendMessageA(status, WM_SETREDRAW, FALSE, 0);
        SetWindowTextA(status, statusText);
        SendMessageA(status, WM_SETREDRAW, TRUE, 0);

        // The DSP controls are hidden while the General tab is active. Forcing
        // an owner-draw repaint on a hidden static can leave its text painted
        // in the parent window until the next tab switch repaints that area.
        if (IsWindowVisible(status)) {
            RedrawWindow(status, nullptr, nullptr,
                RDW_ERASE | RDW_INVALIDATE | RDW_UPDATENOW);
        }
    }

    updateProcessingChainUi(hWnd);
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

static void loadDspUiFromConfig(HWND hWnd, const Config* cfg) {
    int gainPos = (int)(cfg->gain * 100.0f);
    if (gainPos < 25) gainPos = 25;
    if (gainPos > 400) gainPos = 400;
    SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_GAIN), TBM_SETPOS, TRUE, gainPos);

    char gainText[32];
    snprintf(gainText, sizeof(gainText), "%.2fx", cfg->gain);
    SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_GAIN), gainText);

    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_EQ), BM_SETCHECK,
        cfg->eqEnabled ? BST_CHECKED : BST_UNCHECKED, 0);

    int presPos = (int)(cfg->eqPresence * 10.0f);
    if (presPos < 0) presPos = 0;
    if (presPos > 80) presPos = 80;
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

    int nrPos = (int)(cfg->nrStrength * 100.0f);
    if (nrPos < 30) nrPos = 30;
    if (nrPos > 95) nrPos = 95;
    SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_NRSTR), TBM_SETPOS, TRUE, nrPos);
    updateNrStrLabel(hWnd);

    HWND backendCombo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    if (backendCombo) {
        const int backend = (_stricmp(cfg->denoiseBackend.c_str(), "dpdfnet") == 0) ? 1 : 0;
        SendMessageA(backendCombo, CB_SETCURSEL, (WPARAM)backend, 0);
    }

    updateDspControlStates(hWnd);
    updateDenoiseBackendUi(hWnd);
}

static void loadGeneralUiFromConfig(HWND hWnd, const Config* cfg) {
    if (!cfg) return;

    selectDeviceInList(GetDlgItem(hWnd, IDC_COMBO_DEVICE), cfg->serial);

    char buf[256];
    setControlTextIfChanged(GetDlgItem(hWnd, IDC_HOST_EDIT), cfg->host.c_str());
    snprintf(buf, sizeof(buf), "%d", cfg->port);
    setControlTextIfChanged(GetDlgItem(hWnd, IDC_PORT_EDIT), buf);

    HWND hAppCombo = GetDlgItem(hWnd, IDC_COMBO_ANDROID_APP);
    if (hAppCombo) {
        int appSel = cfg->androidAppPreset;
        if (appSel < 0 || appSel > 1) appSel = 0;
        SendMessageA(hAppCombo, CB_SETCURSEL, (WPARAM)appSel, 0);
    }

    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NS), BM_SETCHECK,
        cfg->nsEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AEC), BM_SETCHECK,
        cfg->aecEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AGC), BM_SETCHECK,
        cfg->agcEnabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageA(GetDlgItem(hWnd, IDC_CHECK_DEBUG), BM_SETCHECK,
        cfg->debugConsole ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void loadAllUiFromConfig(HWND hWnd, const Config* cfg) {
    if (!cfg) return;
    loadGeneralUiFromConfig(hWnd, cfg);
    loadDspUiFromConfig(hWnd, cfg);
}

static void saveDspUiToConfig(HWND hWnd, Config* cfg) {
    int gainPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_GAIN), TBM_GETPOS, 0, 0);
    cfg->gain = (float)gainPos / 100.0f;
    if (cfg->gain < 0.25f) cfg->gain = 0.25f;
    if (cfg->gain > 4.0f) cfg->gain = 4.0f;

    cfg->eqEnabled = isChecked(hWnd, IDC_CHECK_EQ);
    int presPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_PRES), TBM_GETPOS, 0, 0);
    cfg->eqPresence = (float)presPos / 10.0f;
    if (cfg->eqPresence < 0.0f) cfg->eqPresence = 0.0f;
    if (cfg->eqPresence > 8.0f) cfg->eqPresence = 8.0f;

    int bassPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_BASS), TBM_GETPOS, 0, 0);
    cfg->eqBassCut = -(float)bassPos / 10.0f;
    if (cfg->eqBassCut < -6.0f) cfg->eqBassCut = -6.0f;
    if (cfg->eqBassCut > 0.0f) cfg->eqBassCut = 0.0f;

    cfg->compressorEnabled = isChecked(hWnd, IDC_CHECK_COMP);
    cfg->nrEnabled = isChecked(hWnd, IDC_CHECK_NR);

    int nrPos = (int)SendMessageA(GetDlgItem(hWnd, IDC_TRACKBAR_NRSTR), TBM_GETPOS, 0, 0);
    cfg->nrStrength = (float)nrPos / 100.0f;
    if (cfg->nrStrength < 0.3f) cfg->nrStrength = 0.3f;
    if (cfg->nrStrength > 0.95f) cfg->nrStrength = 0.95f;

    HWND backendCombo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    const int backend = backendCombo
        ? (int)SendMessageA(backendCombo, CB_GETCURSEL, 0, 0)
        : 0;
    cfg->denoiseBackend = (backend == 1) ? "dpdfnet" : "rnnoise";
}

static bool saveUiToConfig(HWND hWnd, Config* cfg) {
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
    char* end = nullptr;
    const long parsedPort = std::strtol(buf, &end, 10);
    if (buf[0] == '\0' || end == buf || *end != '\0' ||
        parsedPort < 1 || parsedPort > 65535) {
        MessageBoxA(hWnd,
            "Port must be an integer from 1 to 65535.",
            "VoxMic - Settings", MB_OK | MB_ICONWARNING);
        HWND hPortEdit = GetDlgItem(hWnd, IDC_PORT_EDIT);
        SetFocus(hPortEdit);
        SendMessageA(hPortEdit, EM_SETSEL, 0, -1);
        return false;
    }
    cfg->port = (int)parsedPort;

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

    cfg->nsEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_NS), BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg->aecEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AEC), BM_GETCHECK, 0, 0) == BST_CHECKED);
    cfg->agcEnabled =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_AGC), BM_GETCHECK, 0, 0) == BST_CHECKED);

    saveDspUiToConfig(hWnd, cfg);

    cfg->debugConsole =
        (SendMessageA(GetDlgItem(hWnd, IDC_CHECK_DEBUG), BM_GETCHECK, 0, 0) == BST_CHECKED);
    return true;
}

static bool saveStartupRegistrationControl(HWND hWnd);
static void refreshStartupRegistrationControl(HWND hWnd);

static int denoiseBackendKind(const Config& cfg) {
    return (_stricmp(cfg.denoiseBackend.c_str(), "dpdfnet") == 0) ? 1 : 0;
}

static void applyDspPreviewFromUi(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData || !pData->pConfig) return;

    Config preview = *pData->pConfig;
    saveDspUiToConfig(hWnd, &preview);

    const bool oldNrEnabled = g_nrEnabled.load(std::memory_order_acquire);
    const int oldBackend = g_denoiseBackend.load(std::memory_order_acquire);
    syncDspAtomsFromConfig(preview);

    if (oldNrEnabled != preview.nrEnabled ||
        oldBackend != denoiseBackendKind(preview)) {
        requestDenoiseReset();
    }

    updateDspControlStates(hWnd);
    updateDenoiseBackendUi(hWnd);
}

static void restoreDspPreviewFromSnapshot(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData || !pData->hasEditBase) return;

    const bool oldNrEnabled = g_nrEnabled.load(std::memory_order_acquire);
    const int oldBackend = g_denoiseBackend.load(std::memory_order_acquire);
    syncDspAtomsFromConfig(pData->editBaseConfig);

    if (oldNrEnabled != pData->editBaseConfig.nrEnabled ||
        oldBackend != denoiseBackendKind(pData->editBaseConfig)) {
        requestDenoiseReset();
    }
}

static void restoreDspAfterFailedCommit(HWND hWnd) {
    restoreDspPreviewFromSnapshot(hWnd);
    updateDspControlStates(hWnd);
    updateDenoiseBackendUi(hWnd);
}

static void beginSettingsEdit(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData || !pData->pConfig) return;

    pData->editBaseConfig = *pData->pConfig;
    pData->hasEditBase = true;
    loadAllUiFromConfig(hWnd, &pData->editBaseConfig);
    // Restoring edit controls can emit EN_CHANGE; clear dirty after loading.
    setSettingsDirty(hWnd, false);
}

static bool commitSettings(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData || !pData->pConfig) return false;

    const Config previous = *pData->pConfig;
    const std::string oldBackend = previous.denoiseBackend;
    const bool oldNrEnabled = previous.nrEnabled;
    const bool wasDpdfnetDegraded =
        g_dpdfnetDegraded.load(std::memory_order_acquire);

    Config committed = previous;
    if (!saveUiToConfig(hWnd, &committed)) return false;

    if (!committed.save()) {
        const bool rolledBack = previous.save();
        restoreDspAfterFailedCommit(hWnd);
        MessageBoxA(hWnd,
            rolledBack
                ? "Failed to save settings to config.ini. No changes were applied. "
                  "Audio preview has reverted to the last saved settings."
                : "Failed to save settings to config.ini. The file may be partially updated. "
                  "Audio preview has reverted to the last saved settings.",
            "VoxMic - Settings", MB_OK | MB_ICONWARNING);
        return false;
    }

    // Registry state is external to config.ini. If this step fails, restore
    // the previous file contents and keep the in-memory config unchanged.
    if (!saveStartupRegistrationControl(hWnd)) {
        if (!previous.save()) {
            restoreDspAfterFailedCommit(hWnd);
            MessageBoxA(hWnd,
                "Startup registration failed, and config.ini could not be rolled back. "
                "Audio preview has reverted to the last saved settings.",
                "VoxMic - Settings", MB_OK | MB_ICONWARNING);
        } else {
            restoreDspAfterFailedCommit(hWnd);
            MessageBoxA(hWnd,
                "Startup registration failed; config.ini was rolled back. "
                "Audio preview has reverted to the last saved settings.",
                "VoxMic - Settings", MB_OK | MB_ICONWARNING);
        }
        return false;
    }

    *pData->pConfig = committed;
    syncDspAtomsFromConfig(*pData->pConfig);

    const bool backendChanged =
        _stricmp(oldBackend.c_str(), pData->pConfig->denoiseBackend.c_str()) != 0;
    const bool nrChanged = oldNrEnabled != pData->pConfig->nrEnabled;
    if (backendChanged || nrChanged || wasDpdfnetDegraded) {
        requestDenoiseReset();
    }

    pData->editBaseConfig = *pData->pConfig;
    pData->hasEditBase = true;
    setSettingsDirty(hWnd, false);
    updateDspControlStates(hWnd);
    updateDenoiseBackendUi(hWnd);
    return true;
}

static void cancelSettings(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData) return;

    restoreDspPreviewFromSnapshot(hWnd);
    if (pData->hasEditBase) {
        loadAllUiFromConfig(hWnd, &pData->editBaseConfig);
    } else if (pData->pConfig) {
        loadAllUiFromConfig(hWnd, pData->pConfig);
    }
    refreshStartupRegistrationControl(hWnd);
    setSettingsDirty(hWnd, false);
}

// 每次打开设置都重新查注册表（注册表是唯一真相，不读缓存）。
// 检测到「已注册但指向旧路径」（Portable 移动后）显示状态提示。
static void refreshStartupRegistrationControl(HWND hWnd) {
    HWND hCheck = GetDlgItem(hWnd, IDC_CHECK_STARTUP);
    HWND hHint = GetDlgItem(hWnd, IDC_LABEL_STARTUP_HINT);
    if (!hCheck || !hHint) return;

    const StartupRegistrationState state = QueryVoxMicStartupRegistration();
    SendMessageA(hCheck, BM_SETCHECK,
        state.registered ? BST_CHECKED : BST_UNCHECKED, 0);

    if (!state.Succeeded()) {
        SetWindowTextA(hHint, "Unable to read Windows startup settings.");
        return;
    }
    if (state.registered && !state.pointsToCurrentExecutable) {
        SetWindowTextA(hHint,
            "Another VoxMic copy is registered; save to update it.");
        return;
    }
    SetWindowTextA(hHint,
        "Registers this copy in Windows startup.");
}

static void resetStartupRegistrationUiToDefault(HWND hWnd) {
    HWND hCheck = GetDlgItem(hWnd, IDC_CHECK_STARTUP);
    HWND hHint = GetDlgItem(hWnd, IDC_LABEL_STARTUP_HINT);
    if (!hCheck || !hHint) return;
    SendMessageA(hCheck, BM_SETCHECK, BST_UNCHECKED, 0);
    SetWindowTextA(hHint,
        "Startup registration will be disabled when you apply these defaults.");
}

// 事务性保存：注册表变更成功后才提交 config.ini 与 DSP 原子量。
// 失败时弹 MessageBox、保留当前页面状态、返回 false。
static bool saveStartupRegistrationControl(HWND hWnd) {
    const bool enable = (SendMessageA(
        GetDlgItem(hWnd, IDC_CHECK_STARTUP), BM_GETCHECK, 0, 0) == BST_CHECKED);
    const DWORD result = SetVoxMicStartupRegistration(enable);
    if (result == ERROR_SUCCESS) return true;

    char buf[256];
    snprintf(buf, sizeof(buf),
        "Failed to update Windows startup registration (error %lu).",
        static_cast<unsigned long>(result));
    MessageBoxA(hWnd, buf, "VoxMic - Startup", MB_OK | MB_ICONWARNING);
    refreshStartupRegistrationControl(hWnd);
    return false;
}

static void rollbackRuntimeConfigToggle(HWND hWnd, const Config& previous) {
    g_config = previous;
    setDemandModeRuntime(previous.demandMode);
    g_alwaysHot.store(previous.alwaysHot, std::memory_order_relaxed);
    if (g_trayIcon) {
        g_trayIcon->setDemandMode(previous.demandMode);
        g_trayIcon->setAlwaysHot(previous.alwaysHot);
    }

    const bool rolledBack = previous.save();
    MessageBoxA(hWnd,
        rolledBack
            ? "The setting changed for this session but could not be saved. It has been rolled back."
            : "The setting changed for this session, but config.ini could not be saved or rolled back.",
        "VoxMic - Settings", MB_OK | MB_ICONWARNING);
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    if (g_trayIcon && g_trayIcon->handleWindowMessage(msg)) {
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA* pCreate = (CREATESTRUCTA*)lParam;
        Config* cfg = (Config*)pCreate->lpCreateParams;
        pData = new SettingsDialogData();
        pData->pConfig = cfg;
        pData->hHintFont = CreateFontA(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, "MS Shell Dlg");
        pData->hSectionFont = CreateFontA(16, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH, "MS Shell Dlg");
        HIGHCONTRASTA highContrast = { sizeof(highContrast) };
        pData->useSystemInputColors =
            SystemParametersInfoA(SPI_GETHIGHCONTRAST,
                sizeof(highContrast), &highContrast, 0) != FALSE &&
            (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        if (!pData->useSystemInputColors) {
            pData->hInputBrush = CreateSolidBrush(RGB(255, 255, 255));
        }
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)pData);

        HINSTANCE hInst = pCreate->hInstance;
        
        pData->hTab = CreateWindowExA(0, WC_TABCONTROLA, "",
            WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE,
            10, 10, 465, 465, hWnd, (HMENU)IDC_TAB_MAIN, hInst, NULL);
            
        TCITEMA tie = {};
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPSTR)"General";
        SendMessageA(pData->hTab, TCM_INSERTITEMA, 0, (LPARAM)&tie);
        tie.pszText = (LPSTR)"DSP";
        SendMessageA(pData->hTab, TCM_INSERTITEMA, 1, (LPARAM)&tie);

        auto addGen = [&](HWND h) { pData->tabGeneralControls.push_back(h); return h; };
        auto addDsp = [&](HWND h) { pData->tabDspControls.push_back(h); return h; };

        const int xMargin = 25;
        const int sectionW = 420;
        const int lblW = 100;
        const int ctrlX = xMargin + lblW + 8;
        const int fieldW = 205;

        auto addGenSection = [&](const char* title, int y) {
            SIZE titleSize = {};
            HDC hdc = GetDC(hWnd);
            HFONT oldFont = nullptr;
            if (hdc && pData->hSectionFont) {
                oldFont = (HFONT)SelectObject(hdc, pData->hSectionFont);
                const int titleLength = lstrlenA(title);
                if (titleLength > 0) {
                    if (!GetTextExtentPoint32A(
                            hdc, title, titleLength, &titleSize)) {
                        titleSize = {};
                    }
                }
                if (oldFont) SelectObject(hdc, oldFont);
                ReleaseDC(hWnd, hdc);
            }

            const int titleW = titleSize.cx > 0 ? titleSize.cx : 150;
            const int lineX = xMargin + titleW + 14;
            const int lineW = sectionW - (lineX - xMargin);

            HWND heading = addGen(CreateWindowExA(0, "STATIC", title,
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                xMargin, y, titleW + 4, 20, hWnd, NULL, hInst, NULL));
            SendMessageA(heading, WM_SETFONT, (WPARAM)pData->hSectionFont, TRUE);

            if (lineW > 8) {
                addGen(CreateWindowExA(0, "STATIC", "",
                    WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                    lineX, y + 10, lineW, 2,
                    hWnd, NULL, hInst, NULL));
            }
        };

        addGenSection("Connection", 43);

        addGen(CreateWindowExA(0, "STATIC", "ADB Device:",
            WS_CHILD | WS_VISIBLE,
            xMargin, 64, lblW, 22, hWnd, NULL, hInst, NULL));

        HWND hCombo = addGen(CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            ctrlX, 64, 215, 200, hWnd, (HMENU)IDC_COMBO_DEVICE, hInst, NULL));
        pData->hDeviceCombo = hCombo;
        SendMessageA(hCombo, CB_SETDROPPEDWIDTH, 300, 0);

        addGen(CreateWindowExA(0, "BUTTON", "Refresh",
            WS_CHILD | WS_VISIBLE,
            ctrlX + 225, 63, 66, 25,
            hWnd, (HMENU)IDC_BTN_REFRESH, hInst, NULL));

        addGen(CreateWindowExA(0, "STATIC", "Host:",
            WS_CHILD | WS_VISIBLE,
            xMargin, 92, lblW, 22, hWnd, NULL, hInst, NULL));

        addGen(CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", cfg->host.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
            ctrlX, 93, fieldW, 21, hWnd, (HMENU)IDC_HOST_EDIT, hInst, NULL));

        addGen(CreateWindowExA(0, "STATIC", "Port:",
            WS_CHILD | WS_VISIBLE,
            xMargin, 120, lblW, 22, hWnd, NULL, hInst, NULL));

        std::string portStr = std::to_string(cfg->port);
        addGen(CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", portStr.c_str(),
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
            ctrlX, 121, 115, 21, hWnd, (HMENU)IDC_PORT_EDIT, hInst, NULL));

        addGenSection("Android Source", 153);
        addGen(CreateWindowExA(0, "STATIC", "Android App:",
            WS_CHILD | WS_VISIBLE,
            xMargin, 174, lblW, 22, hWnd, NULL, hInst, NULL));

        HWND hAppCombo = addGen(CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            ctrlX, 174, 230, 200, hWnd, (HMENU)IDC_COMBO_ANDROID_APP, hInst, NULL));
        pData->hAndroidAppCombo = hAppCombo;

        SendMessageA(hAppCombo, CB_SETDROPPEDWIDTH, 320, 0);
        SendMessageA(hAppCombo, CB_ADDSTRING, 0, (LPARAM)"Legacy AudioSource");
        SendMessageA(hAppCombo, CB_ADDSTRING, 0, (LPARAM)"VoxMic Source (48 kHz)");

        int appSel = cfg->androidAppPreset;
        if (appSel < 0 || appSel > 1) appSel = 0;
        SendMessageA(hAppCombo, CB_SETCURSEL, (WPARAM)appSel, 0);

        addGen(CreateWindowExA(0, "STATIC", "Gain:",
            WS_CHILD | WS_VISIBLE,
            xMargin, 206, lblW, 22, hWnd, NULL, hInst, NULL));

        addGen(CreateWindowExA(0, "STATIC", "",
            WS_CHILD | WS_VISIBLE,
            ctrlX + 200, 206, 65, 22, hWnd, (HMENU)IDC_LABEL_GAIN, hInst, NULL));

        HWND hGainTrackbar = addGen(CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_TOOLTIPS,
            ctrlX, 207, 190, 24, hWnd, (HMENU)IDC_TRACKBAR_GAIN, hInst, NULL));
        SendMessageA(hGainTrackbar, TBM_SETRANGE, TRUE, MAKELONG(25, 400));
        SendMessageA(hGainTrackbar, TBM_SETTICFREQ, 25, 0);

        addGenSection("Android Audio Effects", 241);
        const int effectX = xMargin + 15;

        addGen(CreateWindowExA(0, "BUTTON", "NoiseSuppressor",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            effectX, 264, 150, 22, hWnd, (HMENU)IDC_CHECK_NS, hInst, NULL));

        addGen(CreateWindowExA(0, "BUTTON", "AcousticEchoCanceler",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            effectX + 170, 264, 190, 22, hWnd, (HMENU)IDC_CHECK_AEC, hInst, NULL));

        addGen(CreateWindowExA(0, "BUTTON", "AutomaticGainControl",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            effectX, 294, 170, 22, hWnd, (HMENU)IDC_CHECK_AGC, hInst, NULL));

        HWND hEffectsHint = addGen(CreateWindowExA(0, "STATIC",
            "Connection, Android effects and debug console changes apply after restart.",
            WS_CHILD | WS_VISIBLE,
            effectX, 322, 395, 16, hWnd, NULL, hInst, NULL));
        pData->hintControls.push_back(hEffectsHint);

        addGenSection("Application", 350);

        addGen(CreateWindowExA(0, "BUTTON", "Show Debug Console",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            xMargin + 15, 373, 220, 22, hWnd, (HMENU)IDC_CHECK_DEBUG, hInst, NULL));

        addGen(CreateWindowExA(0, "BUTTON", "Start VoxMic with Windows",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            xMargin + 15, 401, 250, 22, hWnd, (HMENU)IDC_CHECK_STARTUP, hInst, NULL));

        HWND hStartupHint = addGen(CreateWindowExA(0, "STATIC",
            "Registers this copy in Windows startup.",
            WS_CHILD | WS_VISIBLE,
            xMargin + 15, 429, 395, 16, hWnd, (HMENU)IDC_LABEL_STARTUP_HINT, hInst, NULL));
        pData->hintControls.push_back(hStartupHint);

        // --- DSP Tab Controls ---
        // Keep the existing 500x565 window height. The DSP page uses the
        // available right-side space for a compact processing-chain summary.
        const int dspX = 20;
        const int dspLabelX = 35;
        const int dspCtrlX = 128;
        const int dspSliderW = 240;
        const int dspValueX = 380;
        const int dspValueW = 55;

        auto addDspGroup = [&](const char* title, int x, int y, int w, int h) {
            HWND group = addDsp(CreateWindowExA(0, "BUTTON", title,
                WS_CHILD | BS_GROUPBOX,
                x, y, w, h, hWnd, NULL, hInst, NULL));
            SendMessageA(group, WM_SETFONT, (WPARAM)pData->hSectionFont, TRUE);
            return group;
        };

        HWND hChainStatus = addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD | SS_LEFT | SS_NOPREFIX,
            dspX + 10, 45, 435, 22, hWnd,
            (HMENU)IDC_LABEL_DSP_CHAIN_STATUS, hInst, NULL));
        SendMessageA(hChainStatus, WM_SETFONT, (WPARAM)pData->hHintFont, TRUE);
        pData->hDspChainStatus = hChainStatus;
        pData->hintControls.push_back(hChainStatus);

        addDspGroup("Noise Reduction", dspX, 70, 445, 165);
        addDsp(CreateWindowExA(0, "BUTTON", "Enable noise reduction",
            WS_CHILD | BS_AUTOCHECKBOX,
            dspLabelX, 91, 170, 22, hWnd, (HMENU)IDC_CHECK_NR, hInst, NULL));

        HWND hBackendLabel = addDsp(CreateWindowExA(0, "STATIC", "Backend:",
            WS_CHILD,
            dspLabelX + 10, 120, 85, 22, hWnd, NULL, hInst, NULL));
        HWND hBackendCombo = addDsp(CreateWindowExA(0, "COMBOBOX", "",
            WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            dspCtrlX, 118, 235, 100, hWnd, (HMENU)IDC_COMBO_NR_BACKEND, hInst, NULL));
        pData->hNrBackendCombo = hBackendCombo;
        SendMessageA(hBackendCombo, CB_ADDSTRING, 0, (LPARAM)"RNNoise (built-in)");
        SendMessageA(hBackendCombo, CB_ADDSTRING, 0, (LPARAM)"DPDFNet (48 kHz model)");

        HWND hBackendStatus = addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD | SS_OWNERDRAW,
            dspLabelX + 10, 147, 400, 38, hWnd,
            (HMENU)IDC_LABEL_NR_BACKEND_STATUS, hInst, NULL));
        SendMessageA(hBackendStatus, WM_SETFONT, (WPARAM)pData->hHintFont, TRUE);

        HWND hNrStrengthLabel = addDsp(CreateWindowExA(0, "STATIC", "NR Strength:",
            WS_CHILD,
            dspLabelX + 10, 187, 85, 22, hWnd, NULL, hInst, NULL));
        addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD,
            dspValueX, 187, dspValueW, 22, hWnd, (HMENU)IDC_LABEL_NRSTR, hInst, NULL));
        HWND hNrStrTrackbar = addDsp(CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | TBS_HORZ | TBS_TOOLTIPS,
            dspCtrlX, 186, dspSliderW, 24, hWnd, (HMENU)IDC_TRACKBAR_NRSTR, hInst, NULL));
        SendMessageA(hNrStrTrackbar, TBM_SETRANGE, TRUE, MAKELONG(30, 95));
        SendMessageA(hNrStrTrackbar, TBM_SETTICFREQ, 10, 0);

        HWND hNrHint = addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD,
            dspLabelX + 10, 215, 400, 16, hWnd, NULL, hInst, NULL));
        pData->hNrStrengthHint = hNrHint;
        pData->hintControls.push_back(hNrHint);
        pData->nrDependentControls.push_back(hBackendLabel);
        pData->nrDependentControls.push_back(hBackendCombo);
        pData->nrDependentControls.push_back(hBackendStatus);
        pData->nrDependentControls.push_back(hNrStrengthLabel);
        pData->nrDependentControls.push_back(hNrHint);

        addDspGroup("Tone / EQ", dspX, 245, 445, 145);
        addDsp(CreateWindowExA(0, "BUTTON", "EQ Enable",
            WS_CHILD | BS_AUTOCHECKBOX,
            dspLabelX, 266, 120, 22, hWnd, (HMENU)IDC_CHECK_EQ, hInst, NULL));

        HWND hPresenceLabel = addDsp(CreateWindowExA(0, "STATIC", "Presence:",
            WS_CHILD,
            dspLabelX, 295, 85, 22, hWnd, NULL, hInst, NULL));
        HWND hPresenceValue = addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD,
            dspValueX, 295, dspValueW, 22, hWnd, (HMENU)IDC_LABEL_PRES, hInst, NULL));
        HWND hPresTrackbar = addDsp(CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | TBS_HORZ | TBS_TOOLTIPS,
            dspCtrlX, 294, dspSliderW, 24, hWnd, (HMENU)IDC_TRACKBAR_PRES, hInst, NULL));
        SendMessageA(hPresTrackbar, TBM_SETRANGE, TRUE, MAKELONG(0, 80));
        SendMessageA(hPresTrackbar, TBM_SETTICFREQ, 10, 0);

        HWND hPresHint = addDsp(CreateWindowExA(0, "STATIC",
            "Boost vocal presence and articulation (1.7-3.7 kHz)",
            WS_CHILD,
            dspLabelX + 10, 322, 400, 16, hWnd, NULL, hInst, NULL));
        pData->hintControls.push_back(hPresHint);

        HWND hBassLabel = addDsp(CreateWindowExA(0, "STATIC", "Bass Cut:",
            WS_CHILD,
            dspLabelX, 345, 85, 22, hWnd, NULL, hInst, NULL));
        HWND hBassValue = addDsp(CreateWindowExA(0, "STATIC", "",
            WS_CHILD,
            dspValueX, 345, dspValueW, 22, hWnd, (HMENU)IDC_LABEL_BASS, hInst, NULL));
        HWND hBassTrackbar = addDsp(CreateWindowExA(0, TRACKBAR_CLASSA, "",
            WS_CHILD | TBS_HORZ | TBS_TOOLTIPS,
            dspCtrlX, 344, dspSliderW, 24, hWnd, (HMENU)IDC_TRACKBAR_BASS, hInst, NULL));
        SendMessageA(hBassTrackbar, TBM_SETRANGE, TRUE, MAKELONG(0, 60));
        SendMessageA(hBassTrackbar, TBM_SETTICFREQ, 10, 0);

        HWND hBassHint = addDsp(CreateWindowExA(0, "STATIC",
            "Reduce low-frequency rumble below 250 Hz",
            WS_CHILD,
            dspLabelX + 10, 372, 400, 16, hWnd, NULL, hInst, NULL));
        pData->hintControls.push_back(hBassHint);

        pData->eqDependentControls.push_back(hPresenceLabel);
        pData->eqDependentControls.push_back(hPresenceValue);
        pData->eqDependentControls.push_back(hPresTrackbar);
        pData->eqDependentControls.push_back(hPresHint);
        pData->eqDependentControls.push_back(hBassLabel);
        pData->eqDependentControls.push_back(hBassValue);
        pData->eqDependentControls.push_back(hBassTrackbar);
        pData->eqDependentControls.push_back(hBassHint);

        addDspGroup("Dynamics", dspX, 400, 445, 65);
        addDsp(CreateWindowExA(0, "BUTTON", "Compressor Enable",
            WS_CHILD | BS_AUTOCHECKBOX,
            dspLabelX, 422, 170, 22, hWnd, (HMENU)IDC_CHECK_COMP, hInst, NULL));
        HWND hCompHint = addDsp(CreateWindowExA(0, "STATIC",
            "Stabilizes voice volume with a fixed voice preset.",
            WS_CHILD,
            dspLabelX + 10, 447, 400, 16, hWnd, (HMENU)IDC_LABEL_COMP_HINT, hInst, NULL));
        pData->hintControls.push_back(hCompHint);

        pData->editBaseConfig = *cfg;
        pData->hasEditBase = true;

        for (HWND hHint : pData->hintControls) {
            SendMessageA(hHint, WM_SETFONT, (WPARAM)pData->hHintFont, TRUE);
        }

        loadAllUiFromConfig(hWnd, cfg);

        // Establish the initial tab visibility explicitly. Some owner-draw
        // child controls can receive an initial paint while the parent is
        // still being created, so do not rely only on the absence of
        // WS_VISIBLE in their creation styles.
        showTabControls(pData, 0);

        int btnY = 490;
        
        CreateWindowExA(0, "BUTTON", "Reset to Defaults",
            WS_CHILD | WS_VISIBLE,
            10, btnY, 145, 25, hWnd, (HMENU)IDC_BTN_RESET, hInst, NULL);

        CreateWindowExA(0, "BUTTON", "Cancel",
            WS_CHILD | WS_VISIBLE,
            240, btnY, 75, 25, hWnd, (HMENU)IDC_BTN_CANCEL, hInst, NULL);

        pData->hApply = CreateWindowExA(0, "BUTTON", "Apply",
            WS_CHILD | WS_VISIBLE,
            325, btnY, 75, 25, hWnd, (HMENU)IDC_BTN_APPLY, hInst, NULL);

        CreateWindowExA(0, "BUTTON", "OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            410, btnY, 75, 25, hWnd, (HMENU)IDC_BTN_OK, hInst, NULL);
        updateDirtyUi(hWnd);

        refreshDeviceList(hCombo, cfg->serial);

        refreshStartupRegistrationControl(hWnd);
        SetTimer(hWnd, ID_TIMER_BACKEND_STATUS, 500, nullptr);

        return 0;
    }

    case WM_SHOWWINDOW:
        // 每次显示都重新查注册表（Portable 移动后旧值失效会被识别）。
        if (wParam) {
            const int selectedTab = (pData && pData->hTab)
                ? (int)SendMessageA(pData->hTab, TCM_GETCURSEL, 0, 0)
                : 0;
            if (pData) {
                beginSettingsEdit(hWnd);
                showTabControls(pData, selectedTab == 1 ? 1 : 0);
            }
            refreshStartupRegistrationControl(hWnd);
            updateDenoiseBackendUi(hWnd);
            // Repaint only the parent background here. Visible children are
            // invalidated by ShowWindow or by their own targeted redraw;
            // including hidden owner-draw children can reproduce the ghost.
            RedrawWindow(hWnd, nullptr, nullptr,
                RDW_ERASE | RDW_INVALIDATE | RDW_NOCHILDREN | RDW_UPDATENOW);
        }
        return 0;

    case WM_TIMER:
        if (wParam == ID_TIMER_BACKEND_STATUS && IsWindowVisible(hWnd)) {
            updateDenoiseBackendUi(hWnd);
        }
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONUP) {
            ShowWindow(hWnd, SW_SHOW);
            SetForegroundWindow(hWnd);
        } else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            if (g_trayIcon) g_trayIcon->showMenu(hWnd);
        }
        return 0;

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* draw = (const DRAWITEMSTRUCT*)lParam;
        if (draw && draw->CtlID == IDC_LABEL_NR_BACKEND_STATUS) {
            FillRect(draw->hDC, &draw->rcItem,
                GetSysColorBrush(COLOR_BTNFACE));

            HFONT oldFont = nullptr;
            if (pData && pData->hHintFont) {
                oldFont = (HFONT)SelectObject(draw->hDC, pData->hHintFont);
            }
            SetBkMode(draw->hDC, TRANSPARENT);
            SetTextColor(draw->hDC, denoiseStatusColor(hWnd));

            char text[512] = {};
            GetWindowTextA(draw->hwndItem, text, (int)sizeof(text));
            RECT textRect = draw->rcItem;
            DrawTextA(draw->hDC, text, -1, &textRect,
                DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);

            if (oldFont) SelectObject(draw->hDC, oldFont);
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLORSTATIC: {
        HWND hCtrl = (HWND)lParam;
        HDC hdc = (HDC)wParam;

        const bool isInputCombo = pData &&
            (hCtrl == pData->hDeviceCombo ||
             hCtrl == pData->hAndroidAppCombo ||
             hCtrl == pData->hNrBackendCombo);
        if (isInputCombo) {
            HBRUSH brush = inputBackgroundBrush(pData);
            SetBkMode(hdc, OPAQUE);
            SetBkColor(hdc, inputBackgroundColor(pData));
            SetTextColor(hdc, IsWindowEnabled(hCtrl)
                ? inputTextColor(pData)
                : GetSysColor(COLOR_GRAYTEXT));
            return (LRESULT)brush;
        }

        if (pData) {
            for (HWND hHint : pData->hintControls) {
                if (hCtrl == hHint) {
                    // These labels change between messages. An opaque
                    // button-face background clears the previous, longer
                    // string before the replacement is painted.
                    SetBkMode(hdc, OPAQUE);
                    SetBkColor(hdc, GetSysColor(COLOR_BTNFACE));
                    SetTextColor(hdc,
                        pData->useSystemInputColors
                            ? GetSysColor(COLOR_GRAYTEXT)
                            : RGB(128, 128, 128));
                    return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
                }
            }
        }

        // Let common controls such as Trackbar keep their native themed
        // painting. Returning a NULL_BRUSH here leaves the trackbar surface
        // unpainted on some Windows themes, which produces a black bar.
        return DefWindowProcA(hWnd, msg, wParam, lParam);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        HBRUSH brush = inputBackgroundBrush(pData);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, inputBackgroundColor(pData));
        SetTextColor(hdc, IsWindowEnabled(hCtrl)
            ? inputTextColor(pData)
            : GetSysColor(COLOR_GRAYTEXT));
        return (LRESULT)brush;
    }

    case WM_CTLCOLORLISTBOX: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        HBRUSH brush = inputBackgroundBrush(pData);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, inputBackgroundColor(pData));
        SetTextColor(hdc, IsWindowEnabled(hCtrl)
            ? inputTextColor(pData)
            : GetSysColor(COLOR_GRAYTEXT));
        return (LRESULT)brush;
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
        bool audioPreviewChanged = false;
        if (hTrackbar == GetDlgItem(hWnd, IDC_TRACKBAR_GAIN)) {
            int pos = (int)SendMessageA(hTrackbar, TBM_GETPOS, 0, 0);
            float g = (float)pos / 100.0f;
            char buf[32];
            snprintf(buf, sizeof(buf), "%.2fx", g);
            SetWindowTextA(GetDlgItem(hWnd, IDC_LABEL_GAIN), buf);
            audioPreviewChanged = true;
        } else if (hTrackbar == GetDlgItem(hWnd, IDC_TRACKBAR_PRES)) {
            updatePresLabel(hWnd);
            audioPreviewChanged = true;
        } else if (hTrackbar == GetDlgItem(hWnd, IDC_TRACKBAR_BASS)) {
            updateBassLabel(hWnd);
            audioPreviewChanged = true;
        } else if (hTrackbar == GetDlgItem(hWnd, IDC_TRACKBAR_NRSTR)) {
            updateNrStrLabel(hWnd);
            audioPreviewChanged = true;
        }
        if (audioPreviewChanged) {
            markSettingsDirty(hWnd);
            applyDspPreviewFromUi(hWnd);
        }
        return 0;
    }

    case WM_COMMAND: {
        WORD id = LOWORD(wParam);
        WORD notify = HIWORD(wParam);
        HWND hCombo = GetDlgItem(hWnd, IDC_COMBO_DEVICE);

        const bool dspToggle = id == IDC_CHECK_EQ ||
            id == IDC_CHECK_COMP || id == IDC_CHECK_NR;
        const bool generalToggle = id == IDC_CHECK_NS ||
            id == IDC_CHECK_AEC || id == IDC_CHECK_AGC ||
            id == IDC_CHECK_DEBUG || id == IDC_CHECK_STARTUP;
        const bool comboChanged = id == IDC_COMBO_DEVICE ||
            id == IDC_COMBO_ANDROID_APP || id == IDC_COMBO_NR_BACKEND;
        const bool editChanged = id == IDC_HOST_EDIT || id == IDC_PORT_EDIT;

        if ((dspToggle || generalToggle) && notify == BN_CLICKED) {
            markSettingsDirty(hWnd);
            if (dspToggle) applyDspPreviewFromUi(hWnd);
        } else if (comboChanged && notify == CBN_SELCHANGE) {
            markSettingsDirty(hWnd);
            if (id == IDC_COMBO_NR_BACKEND) applyDspPreviewFromUi(hWnd);
        } else if (editChanged && notify == EN_CHANGE) {
            markSettingsDirty(hWnd);
        }

        switch (id) {
        case IDC_BTN_REFRESH:
            refreshDeviceList(hCombo, pData->pConfig->serial);
            break;
        case IDC_COMBO_NR_BACKEND:
            if (notify == CBN_SELCHANGE) updateDenoiseBackendUi(hWnd);
            break;
        case IDC_BTN_RESET: {
            Config defaultCfg;
            loadAllUiFromConfig(hWnd, &defaultCfg);
            resetStartupRegistrationUiToDefault(hWnd);
            markSettingsDirty(hWnd);
            applyDspPreviewFromUi(hWnd);
            break;
        }
        case IDC_BTN_APPLY:
            commitSettings(hWnd);
            break;
        case IDC_BTN_OK: {
            if (commitSettings(hWnd)) ShowWindow(hWnd, SW_HIDE);
            break;
        }
        case IDC_BTN_CANCEL:
            cancelSettings(hWnd);
            ShowWindow(hWnd, SW_HIDE);
            break;

        case ID_MENU_DEMAND_MODE: {
            const Config previous = g_config;
            bool newVal = !g_demandMode.load();
            setDemandModeRuntime(newVal);
            if (g_trayIcon) g_trayIcon->setDemandMode(newVal);
            g_config.demandMode = newVal;
            if (!g_config.save()) {
                rollbackRuntimeConfigToggle(hWnd, previous);
                break;
            }
            if (newVal)
                printf("[Demand] Mode ON (mic monitor active)\n");
            else
                printf("[Demand] Mode OFF (always stream)\n");
            fflush(stdout);
            break;
        }
        case ID_MENU_ALWAYS_HOT: {
            const Config previous = g_config;
            bool newVal = !g_alwaysHot.load();
            g_alwaysHot.store(newVal);
            if (g_trayIcon) g_trayIcon->setAlwaysHot(newVal);
            g_config.alwaysHot = newVal;
            if (!g_config.save()) {
                rollbackRuntimeConfigToggle(hWnd, previous);
                break;
            }
            if (newVal)
                printf("[AlwaysHot] ON (socket always connected)\n");
            else
                printf("[AlwaysHot] OFF (socket disconnect on idle)\n");
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
        SendMessageA(hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_CANCEL, 0), 0);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER_BACKEND_STATUS);
        if (pData && pData->hHintFont) {
            DeleteObject(pData->hHintFont);
            pData->hHintFont = NULL;
        }
        if (pData && pData->hSectionFont) {
            DeleteObject(pData->hSectionFont);
            pData->hSectionFont = NULL;
        }
        if (pData && pData->hInputBrush) {
            DeleteObject(pData->hInputBrush);
            pData->hInputBrush = NULL;
        }
        delete pData;
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, 0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
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
        0, 0, 500, 565,
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
