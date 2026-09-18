#include "settings_dialog.h"

#include <commctrl.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "adb_control.h"
#include "app_state.h"
#include "settings_control_ids.h"
#include "settings_fields.h"
#include "settings_layout.h"
#include "settings_metrics.h"
#include "settings_theme.h"
#include "startup_registration.h"
#include "tray_icon.h"

#define SETTINGS_CLASS "VoxMicSettingsClass"

struct SettingsDialogData {
    Config* pConfig = nullptr;
    Config editBaseConfig;
    bool hasEditBase = false;
    bool dirty = false;
    settings::Layout layout;
    HWND hApply = nullptr;
    HWND hDeviceCombo = nullptr;
    HWND hAndroidAppCombo = nullptr;
    HWND hNrBackendCombo = nullptr;
    HWND hDspChainStatus = nullptr;
    HWND hNrStrengthHint = nullptr;
    HBRUSH hInputBrush = nullptr;
    bool useSystemInputColors = false;
};

// High contrast is an accessibility setting, so when the user has it on the
// window follows the system palette instead of the design tokens. Everywhere
// else reads one token and names no colour of its own.
static bool useSystemPalette(const SettingsDialogData* pData) {
    return pData && pData->useSystemInputColors;
}

static COLORREF inputBackgroundColor(const SettingsDialogData* pData) {
    return useSystemPalette(pData) ? GetSysColor(COLOR_WINDOW) : theme::Panel;
}

static COLORREF inputTextColor(const SettingsDialogData* pData) {
    return useSystemPalette(pData) ? GetSysColor(COLOR_WINDOWTEXT)
                                   : theme::TextPrimary;
}

static COLORREF labelTextColor(const SettingsDialogData* pData) {
    return useSystemPalette(pData) ? GetSysColor(COLOR_BTNTEXT) : theme::TextPrimary;
}

static COLORREF hintTextColor(const SettingsDialogData* pData) {
    return useSystemPalette(pData) ? GetSysColor(COLOR_GRAYTEXT) : theme::TextHint;
}

static COLORREF disabledTextColor(const SettingsDialogData* pData) {
    return useSystemPalette(pData) ? GetSysColor(COLOR_GRAYTEXT)
                                   : theme::TextDisabled;
}

static HBRUSH inputBackgroundBrush(const SettingsDialogData* pData) {
    if (useSystemPalette(pData)) return GetSysColorBrush(COLOR_WINDOW);
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

// Applies every `dependsOn` relation declared in the field table, so a new
// "X off disables Y" rule is one field-table entry rather than a new branch
// here. Two relations the table cannot express are handled after the loop.
static void updateDspControlStates(HWND hWnd) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(
        hWnd, GWLP_USERDATA);
    if (!pData) return;

    for (int p = 0; p < settings::kPageCount; ++p) {
        const settings::PageSpec& page = settings::kPages[p];
        for (int s = 0; s < page.count; ++s) {
            const settings::SectionSpec& section = page.sections[s];
            for (int f = 0; f < section.count; ++f) {
                const settings::FieldSpec& field = section.fields[f];
                if (!field.gated()) continue;
                const bool on = isChecked(hWnd, field.dependsOn);
                for (HWND control : pData->layout.fieldWidgets(field.id)) {
                    setControlEnabledIfChanged(control, on);
                }
            }
        }
    }

    // The selected backend gates NR strength too, and that is not a plain
    // toggle dependency: strength only applies when RNNoise is selected.
    HWND backendCombo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    const int selection = backendCombo
        ? (int)SendMessageA(backendCombo, CB_GETCURSEL, 0, 0)
        : 0;
    const bool nrEnabled = isChecked(hWnd, IDC_CHECK_NR);
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
                g_appState.dpdfnetDegraded.load(std::memory_order_acquire) ||
                !g_appState.dpdfnetAvailable.load(std::memory_order_acquire);
            if (fallback) {
                denoiseName = "RNNoise fallback";
            } else if (g_appState.denoiseEffectiveBackend.load(
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

// The backend status line is drawn in the engine's state colour, so it reads
// like the tray icon: streaming = running, armed = ready-but-waiting, degraded
// = fell back, idle = switched off.
static COLORREF denoiseStatusColor(HWND hWnd) {
    if (!isChecked(hWnd, IDC_CHECK_NR)) return theme::StateIdle;

    HWND combo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    const int selection = combo ? (int)SendMessageA(combo, CB_GETCURSEL, 0, 0) : 0;
    const bool available = g_appState.dpdfnetAvailable.load(std::memory_order_acquire);
    const bool effectiveIsDpdfnet =
        g_appState.denoiseEffectiveBackend.load(std::memory_order_acquire) == 1;
    if (selection == 1) {
        if (g_appState.dpdfnetDegraded.load(std::memory_order_acquire) || !available) {
            return theme::StateDegraded;
        }
        return effectiveIsDpdfnet ? theme::StateStreaming : theme::StateArmed;
    }
    return effectiveIsDpdfnet ? theme::StateArmed : theme::StateStreaming;
}

// Loading is on demand, so "unavailable" is the wrong word for the common case
// where nothing has been loaded yet because RNNoise is selected. Only Failed is
// a real problem.
static const char* dpdfnetNotReadyText() {
    const int s = g_appState.dpdfnetLoadState.load(std::memory_order_acquire);
    if (s == static_cast<int>(DpdfnetLoadState::Loading))
        return "DPDFNet is loading; audio uses RNNoise until it is ready.";
    if (s == static_cast<int>(DpdfnetLoadState::Failed))
        return "DPDFNet is unavailable; audio will use RNNoise fallback.";
    if (s == static_cast<int>(DpdfnetLoadState::NotLoaded))
        return "DPDFNet is not loaded; selecting it will load it on demand.";
    return "DPDFNet is ready; it will take effect at the next audio block.";
}

static void updateDenoiseBackendUi(HWND hWnd) {
    HWND combo = GetDlgItem(hWnd, IDC_COMBO_NR_BACKEND);
    HWND status = GetDlgItem(hWnd, IDC_LABEL_NR_BACKEND_STATUS);
    if (!combo || !status) return;

    const int selection = (int)SendMessageA(combo, CB_GETCURSEL, 0, 0);
    const bool dpdfnetRequested = selection == 1;
    const bool nrEnabled = g_appState.nrEnabled.load(std::memory_order_acquire);
    updateDspControlStates(hWnd);

    const int activeRequestedBackend = g_appState.denoiseBackend.load(
        std::memory_order_acquire);
    const bool selectionIsApplied = activeRequestedBackend ==
        (dpdfnetRequested ? 1 : 0);
    const char* statusText = nullptr;
    if (!nrEnabled) {
        statusText = "Noise reduction is disabled; enable it to use the selected backend.";
    } else if (!dpdfnetRequested) {
        statusText = selectionIsApplied
            ? (g_appState.denoiseEffectiveBackend.load(std::memory_order_acquire) == 0
                ? "RNNoise is active."
                : "RNNoise is selected; it will take effect at the next audio block.")
            : "RNNoise is selected; it will take effect at the next audio block.";
    } else if (!selectionIsApplied) {
        statusText = g_appState.dpdfnetAvailable.load(std::memory_order_acquire)
            ? "DPDFNet is ready; it will take effect at the next audio block."
            : dpdfnetNotReadyText();
    } else if (g_appState.dpdfnetDegraded.load(std::memory_order_acquire)) {
        statusText =
            "DPDFNet was degraded to RNNoise after a worker stall; it will retry after the next stream reset.";
    } else if (!g_appState.dpdfnetAvailable.load(std::memory_order_acquire)) {
        statusText = dpdfnetNotReadyText();
    } else if (g_appState.denoiseEffectiveBackend.load(std::memory_order_acquire) == 1) {
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

// The settings UI has two pages, and both are described entirely by the field
// table, so nothing below walks a hand-written list of controls.
template <typename Fn>
static void forEachField(Fn&& fn) {
    for (int p = 0; p < settings::kPageCount; ++p) {
        const settings::PageSpec& page = settings::kPages[p];
        for (int s = 0; s < page.count; ++s) {
            const settings::SectionSpec& section = page.sections[s];
            for (int f = 0; f < section.count; ++f) {
                fn(section.fields[f]);
            }
        }
    }
}

template <typename Fn>
static void forEachFieldOnPage(int pageIndex, Fn&& fn) {
    if (pageIndex < 0 || pageIndex >= settings::kPageCount) return;
    const settings::PageSpec& page = settings::kPages[pageIndex];
    for (int s = 0; s < page.count; ++s) {
        const settings::SectionSpec& section = page.sections[s];
        for (int f = 0; f < section.count; ++f) {
            fn(section.fields[f]);
        }
    }
}

// Slider positions are trackbar units; the config value is position * scale, so
// the label is formatted from the config value and never from the raw position.
static void updateSliderLabel(HWND hWnd, const settings::FieldSpec& field) {
    if (!field.valueId) return;
    HWND track = GetDlgItem(hWnd, field.id);
    if (!track) return;
    const int pos = (int)SendMessageA(track, TBM_GETPOS, 0, 0);
    char buf[32];
    snprintf(buf, sizeof(buf), field.slider.fmt,
             static_cast<double>(pos * field.slider.scale));
    SetWindowTextA(GetDlgItem(hWnd, field.valueId), buf);
}

static void loadAllUiFromConfig(HWND hWnd, const Config* cfg) {
    if (!cfg) return;

    forEachField([&](const settings::FieldSpec& field) {
        // Every binding test also checks that the table row actually names a
        // target: the table is edited by hand, and a row that forgets its
        // binding must be a no-op, not a null member pointer dereference.
        switch (field.kind) {
            case settings::FieldKind::Toggle:
                if (field.bind == settings::Bind::Bool && field.bBool) {
                    SendMessageA(GetDlgItem(hWnd, field.id), BM_SETCHECK,
                                 (cfg->*field.bBool) ? BST_CHECKED : BST_UNCHECKED,
                                 0);
                }
                break;
            case settings::FieldKind::Text:
                if (field.bind == settings::Bind::Str && field.bStr) {
                    setControlTextIfChanged(GetDlgItem(hWnd, field.id),
                                            (cfg->*field.bStr).c_str());
                }
                break;
            case settings::FieldKind::Number:
                if (field.bind == settings::Bind::Int && field.bInt) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%d", cfg->*field.bInt);
                    setControlTextIfChanged(GetDlgItem(hWnd, field.id), buf);
                }
                break;
            case settings::FieldKind::Slider:
                if (field.bind == settings::Bind::Float && field.bFloat) {
                    const float value = cfg->*field.bFloat;
                    const float scaled = (field.slider.scale != 0.0f)
                                             ? value / field.slider.scale
                                             : 0.0f;
                    int pos = (int)(scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f);
                    if (pos < field.slider.min) pos = field.slider.min;
                    if (pos > field.slider.max) pos = field.slider.max;
                    SendMessageA(GetDlgItem(hWnd, field.id), TBM_SETPOS, TRUE, pos);
                    updateSliderLabel(hWnd, field);
                }
                break;
            case settings::FieldKind::Choice: {
                HWND combo = GetDlgItem(hWnd, field.id);
                if (!combo) break;
                if (field.bind == settings::Bind::Int && field.bInt) {
                    int sel = cfg->*field.bInt;
                    if (sel < 0 || (field.optionCount > 0 && sel >= field.optionCount)) {
                        sel = 0;
                    }
                    SendMessageA(combo, CB_SETCURSEL, (WPARAM)sel, 0);
                } else if (field.bind == settings::Bind::StrEnum && field.bStr &&
                           field.enumValues) {
                    const std::string& value = cfg->*field.bStr;
                    int sel = 0;
                    for (int i = 0; i < field.enumCount; ++i) {
                        if (_stricmp(value.c_str(), field.enumValues[i]) == 0) {
                            sel = i;
                            break;
                        }
                    }
                    SendMessageA(combo, CB_SETCURSEL, (WPARAM)sel, 0);
                }
                break;
            }
            default:
                break;
        }
    });

    // The device list is filled from `adb devices` at runtime, so it is selected
    // by serial instead of by index.
    selectDeviceInList(GetDlgItem(hWnd, IDC_COMBO_DEVICE), cfg->serial);

    updateDspControlStates(hWnd);
    updateDenoiseBackendUi(hWnd);
}

// Writes one field's current value into cfg. With `validate` set, a Number field
// outside its declared range reports to the user and returns false; without it
// the value is clamped silently, which is what the live preview path needs
// because it runs on every slider drag.
static bool writeFieldToConfig(HWND hWnd, const settings::FieldSpec& field,
                               Config* cfg, bool validate) {
    // Each binding test also checks the table row named a target; see the note
    // in loadAllUiFromConfig.
    switch (field.kind) {
        case settings::FieldKind::Toggle:
            if (field.bind == settings::Bind::Bool && field.bBool) {
                cfg->*field.bBool = isChecked(hWnd, field.id);
            }
            break;
        case settings::FieldKind::Text:
            if (field.bind == settings::Bind::Str && field.bStr) {
                char buf[256] = {};
                GetWindowTextA(GetDlgItem(hWnd, field.id), buf,
                               static_cast<int>(sizeof(buf)));
                cfg->*field.bStr = buf;
            }
            break;
        case settings::FieldKind::Number:
            if (field.bind == settings::Bind::Int && field.bInt) {
                char buf[64] = {};
                HWND edit = GetDlgItem(hWnd, field.id);
                GetWindowTextA(edit, buf, static_cast<int>(sizeof(buf)));
                char* end = nullptr;
                const long parsed = std::strtol(buf, &end, 10);
                const bool inRange = buf[0] != '\0' && end != buf && *end == '\0' &&
                                     parsed >= field.intMin && parsed <= field.intMax;
                if (!inRange) {
                    if (!validate) return false;
                    char message[256];
                    snprintf(message, sizeof(message),
                             "%s must be an integer from %d to %d.",
                             field.label ? field.label : "This field",
                             field.intMin, field.intMax);
                    MessageBoxA(hWnd, message, "VoxMic - Settings",
                                MB_OK | MB_ICONWARNING);
                    SetFocus(edit);
                    SendMessageA(edit, EM_SETSEL, 0, -1);
                    return false;
                }
                cfg->*field.bInt = static_cast<int>(parsed);
            }
            break;
        case settings::FieldKind::Slider:
            if (field.bind == settings::Bind::Float && field.bFloat) {
                const int pos = (int)SendMessageA(GetDlgItem(hWnd, field.id),
                                                  TBM_GETPOS, 0, 0);
                float value = pos * field.slider.scale;
                if (value < field.slider.lo) value = field.slider.lo;
                if (value > field.slider.hi) value = field.slider.hi;
                cfg->*field.bFloat = value;
            }
            break;
        case settings::FieldKind::Choice: {
            HWND combo = GetDlgItem(hWnd, field.id);
            const int sel =
                combo ? (int)SendMessageA(combo, CB_GETCURSEL, 0, 0) : 0;
            if (field.bind == settings::Bind::Int && field.bInt) {
                cfg->*field.bInt = sel;
            } else if (field.bind == settings::Bind::StrEnum && field.bStr &&
                       field.enumValues) {
                cfg->*field.bStr = (sel >= 0 && sel < field.enumCount)
                                       ? field.enumValues[sel]
                                       : "";
            }
            break;
        }
        default:
            break;
    }
    return true;
}

static void saveDspUiToConfig(HWND hWnd, Config* cfg) {
    if (!cfg) return;
    forEachFieldOnPage(1, [&](const settings::FieldSpec& field) {
        writeFieldToConfig(hWnd, field, cfg, false);
    });
}

static bool saveUiToConfig(HWND hWnd, Config* cfg) {
    if (!cfg) return false;

    bool ok = true;
    forEachField([&](const settings::FieldSpec& field) {
        if (!ok) return;
        if (!writeFieldToConfig(hWnd, field, cfg, true)) ok = false;
    });
    if (!ok) return false;

    // The device serial is not a config value the user types: it is whichever
    // entry of the runtime-populated device list is selected.
    char buf[256] = {};
    const int deviceIdx = (int)SendMessageA(GetDlgItem(hWnd, IDC_COMBO_DEVICE),
                                            CB_GETCURSEL, 0, 0);
    if (deviceIdx <= 0) {
        cfg->serial.clear();
    } else {
        SendMessageA(GetDlgItem(hWnd, IDC_COMBO_DEVICE), CB_GETLBTEXT,
                     (WPARAM)deviceIdx, (LPARAM)buf);
        cfg->serial = buf;
    }

    // Selecting an app preset also picks the socket name and the Android
    // component to launch; the preset alone is not enough to drive the bridge.
    if (cfg->androidAppPreset == 1) {
        cfg->androidSocket = "voxmicsource";
        cfg->androidComponent = "com.voxmic.source/.MainActivity";
    } else {
        cfg->androidSocket = "audiosource";
        cfg->androidComponent = "fr.dzx.audiosource/.MainActivity";
    }
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

    const bool oldNrEnabled = g_appState.nrEnabled.load(std::memory_order_acquire);
    const int oldBackend = g_appState.denoiseBackend.load(std::memory_order_acquire);
    syncDspAtomsFromConfig(preview, false);

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

    const bool oldNrEnabled = g_appState.nrEnabled.load(std::memory_order_acquire);
    const int oldBackend = g_appState.denoiseBackend.load(std::memory_order_acquire);
    syncDspAtomsFromConfig(pData->editBaseConfig, false);

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
        g_appState.dpdfnetDegraded.load(std::memory_order_acquire);

    Config committed = previous;
    if (!saveUiToConfig(hWnd, &committed)) return false;

    const auto committedSave = committed.save();
    if (!committedSave) {
        const bool rolledBack = previous.save().has_value();
        printf("ERROR: config.ini write failed (%s)\n",
            describe(committedSave.error()).c_str());
        fflush(stdout);
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
        if (!previous.save().has_value()) {
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
    syncDspAtomsFromConfig(*pData->pConfig, true);

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
    g_appState.config = previous;
    setDemandModeRuntime(previous.demandMode);
    g_appState.alwaysHot.store(previous.alwaysHot, std::memory_order_relaxed);
    if (g_appState.trayIcon) {
        g_appState.trayIcon->setDemandMode(previous.demandMode);
        g_appState.trayIcon->setAlwaysHot(previous.alwaysHot);
    }

    const bool rolledBack = previous.save().has_value();
    MessageBoxA(hWnd,
        rolledBack
            ? "The setting changed for this session but could not be saved. It has been rolled back."
            : "The setting changed for this session, but config.ini could not be saved or rolled back.",
        "VoxMic - Settings", MB_OK | MB_ICONWARNING);
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    SettingsDialogData* pData = (SettingsDialogData*)GetWindowLongPtrA(hWnd, GWLP_USERDATA);

    if (g_appState.trayIcon && g_appState.trayIcon->handleWindowMessage(msg)) {
        return 0;
    }

    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCTA* pCreate = (CREATESTRUCTA*)lParam;
        Config* cfg = (Config*)pCreate->lpCreateParams;
        pData = new SettingsDialogData();
        pData->pConfig = cfg;
        HIGHCONTRASTA highContrast = { sizeof(highContrast) };
        pData->useSystemInputColors =
            SystemParametersInfoA(SPI_GETHIGHCONTRAST,
                sizeof(highContrast), &highContrast, 0) != FALSE &&
            (highContrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        if (!pData->useSystemInputColors) {
            pData->hInputBrush = CreateSolidBrush(theme::Panel);
        }
        SetWindowLongPtrA(hWnd, GWLP_USERDATA, (LONG_PTR)pData);

        // The tab, every field and the footer are built from the field table by
        // the layout engine. Nothing below this line places a control: geometry
        // lives in settings_metrics.h, and a new option is one table row.
        metrics::setDpi(GetDpiForWindow(hWnd));
        pData->layout.build(hWnd, pCreate->hInstance);

        pData->hApply = pData->layout.footerButton(IDC_BTN_APPLY);
        pData->hDeviceCombo = pData->layout.control(IDC_COMBO_DEVICE);
        pData->hAndroidAppCombo = pData->layout.control(IDC_COMBO_ANDROID_APP);
        pData->hNrBackendCombo = pData->layout.control(IDC_COMBO_NR_BACKEND);
        pData->hDspChainStatus = pData->layout.control(IDC_LABEL_DSP_CHAIN_STATUS);
        pData->hNrStrengthHint = pData->layout.hint(IDC_TRACKBAR_NRSTR);

        pData->editBaseConfig = *cfg;
        pData->hasEditBase = true;

        // The device list is populated from `adb devices` first, so the config
        // load afterwards can select the saved serial out of it.
        refreshDeviceList(pData->hDeviceCombo, cfg->serial);
        loadAllUiFromConfig(hWnd, cfg);
        updateDirtyUi(hWnd);

        refreshStartupRegistrationControl(hWnd);
        SetTimer(hWnd, ID_TIMER_BACKEND_STATUS, 500, nullptr);

        return 0;
    }

    case WM_SHOWWINDOW:
        // 每次显示都重新查注册表（Portable 移动后旧值失效会被识别）。
        if (wParam) {
            HWND tab = GetDlgItem(hWnd, IDC_TAB_MAIN);
            const int selectedTab =
                tab ? (int)SendMessageA(tab, TCM_GETCURSEL, 0, 0) : 0;
            if (pData) {
                beginSettingsEdit(hWnd);
                pData->layout.showPage(selectedTab == 1 ? 1 : 0);
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
            if (g_appState.trayIcon) g_appState.trayIcon->showMenu(hWnd);
        }
        return 0;

    case WM_DRAWITEM: {
        const DRAWITEMSTRUCT* draw = (const DRAWITEMSTRUCT*)lParam;
        if (draw && draw->CtlID == IDC_LABEL_NR_BACKEND_STATUS) {
            FillRect(draw->hDC, &draw->rcItem,
                (pData && pData->layout.panelBrush())
                    ? pData->layout.panelBrush()
                    : GetSysColorBrush(COLOR_BTNFACE));

            HFONT oldFont = nullptr;
            if (pData && pData->layout.bodyFont()) {
                oldFont = (HFONT)SelectObject(draw->hDC, pData->layout.bodyFont());
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

        // Nearly every text widget paints with a transparent background so it
        // blends into the page surface the layout puts behind it; the ones whose
        // text changes at runtime take the surface colour as an opaque
        // background so the previous, longer string is cleared first.
        const settings::TextRole role = settings::TextRoleOf(hCtrl);
        if (role == settings::TextRole::Transparent) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, IsWindowEnabled(hCtrl) ? labelTextColor(pData)
                                                     : disabledTextColor(pData));
            return (LRESULT)GetStockObject(NULL_BRUSH);
        }
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc,
            pData && pData->layout.panelBrush()
                ? theme::Panel
                : GetSysColor(COLOR_BTNFACE));
        SetTextColor(hdc, role == settings::TextRole::Hint
                              ? hintTextColor(pData)
                              : (IsWindowEnabled(hCtrl) ? labelTextColor(pData)
                                                        : disabledTextColor(pData)));
        if (pData && pData->layout.panelBrush()) {
            return (LRESULT)pData->layout.panelBrush();
        }
        return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
    }

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wParam;
        HWND hCtrl = (HWND)lParam;
        HBRUSH brush = inputBackgroundBrush(pData);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, inputBackgroundColor(pData));
        SetTextColor(hdc, IsWindowEnabled(hCtrl)
            ? inputTextColor(pData)
            : disabledTextColor(pData));
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
            : disabledTextColor(pData));
        return (LRESULT)brush;
    }

    // Push buttons are painted by the theme, but the window still owns the
    // background behind their rounded corners - without this the buttons carry
    // a faint box of the wrong shade.
    case WM_CTLCOLORBTN: {
        HDC hdc = (HDC)wParam;
        if (pData && pData->layout.windowBrush()) {
            return (LRESULT)pData->layout.windowBrush();
        }
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    // The window class carries no background brush of its own, so the window
    // fills itself here. That is also what stops the wrong-shade band that a
    // default brush would paint between the frame and the tab control.
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH brush = (pData && pData->layout.windowBrush())
                           ? pData->layout.windowBrush()
                           : GetSysColorBrush(COLOR_BTNFACE);
        FillRect((HDC)wParam, &rc, brush);
        return 1;
    }

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc;
        GetClientRect(hWnd, &rc);
        HBRUSH brush = (pData && pData->layout.windowBrush())
                           ? pData->layout.windowBrush()
                           : GetSysColorBrush(COLOR_BTNFACE);
        FillRect(hdc, &rc, brush);
        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_NOTIFY: {
        LPNMHDR lpnmhdr = (LPNMHDR)lParam;
        if (lpnmhdr->idFrom == IDC_TAB_MAIN && lpnmhdr->code == TCN_SELCHANGE) {
            HWND tab = GetDlgItem(hWnd, IDC_TAB_MAIN);
            const int sel = tab ? (int)SendMessageA(tab, TCM_GETCURSEL, 0, 0) : 0;
            pData->layout.showPage(sel);
        }
        return 0;
    }

    case WM_HSCROLL: {
        HWND hTrackbar = (HWND)lParam;
        bool audioPreviewChanged = false;
        forEachField([&](const settings::FieldSpec& field) {
            if (field.kind != settings::FieldKind::Slider) return;
            if (GetDlgItem(hWnd, field.id) != hTrackbar) return;
            updateSliderLabel(hWnd, field);
            audioPreviewChanged = true;
        });
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
            const Config previous = g_appState.config;
            bool newVal = !g_appState.demandMode.load();
            setDemandModeRuntime(newVal);
            if (g_appState.trayIcon) g_appState.trayIcon->setDemandMode(newVal);
            g_appState.config.demandMode = newVal;
            if (!g_appState.config.save().has_value()) {
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
            const Config previous = g_appState.config;
            bool newVal = !g_appState.alwaysHot.load();
            g_appState.alwaysHot.store(newVal);
            if (g_appState.trayIcon) g_appState.trayIcon->setAlwaysHot(newVal);
            g_appState.config.alwaysHot = newVal;
            if (!g_appState.config.save().has_value()) {
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
            if (g_appState.trayIcon) g_appState.trayIcon->destroy();
            g_appState.running.store(false);
            PostQuitMessage(0);
            break;
        }
        return 0;
    }

    case WM_SIZE:
        // The window is fixed-size, so a resize only happens when the DPI
        // changed or when the layout had to grow it to fit its tallest page.
        if (pData) pData->layout.relayout(hWnd);
        return 0;

    case WM_DPICHANGED: {
        // The window is per-monitor DPI aware (see src/app.manifest), so it has
        // to rescale itself when it moves to a monitor with a different scale.
        metrics::setDpi(HIWORD(wParam));
        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (pData) pData->layout.relayout(hWnd);
        InvalidateRect(hWnd, nullptr, TRUE);
        return 0;
    }

    case WM_CLOSE:
        SendMessageA(hWnd, WM_COMMAND, MAKEWPARAM(IDC_BTN_CANCEL, 0), 0);
        return 0;

    case WM_DESTROY:
        KillTimer(hWnd, ID_TIMER_BACKEND_STATUS);
        if (pData) {
            // Fonts and brushes belong to the layout engine, which made them.
            pData->layout.destroyResources();
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

    // Created at the system DPI and corrected to the window's real monitor DPI
    // in WM_CREATE, which is where GetDpiForWindow becomes valid.
    metrics::setDpi(GetDpiForSystem());

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = SettingsWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = SETTINGS_CLASS;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    // No background brush on the class: the window paints itself in WM_PAINT
    // and WM_ERASEBKGND with the theme's window colour, so nothing can show the
    // wrong shade anywhere the theme does not reach.
    wc.hbrBackground = nullptr;
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME,
        SETTINGS_CLASS,
        "VoxMic - Settings",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, metrics::S(metrics::WinW), metrics::S(metrics::WinH),
        NULL, NULL, hInstance, pConfig);

    if (!hWnd) return NULL;

    // Centre on the work area and never exceed it: on a scaled display the
    // design size can be taller than the usable screen.
    RECT work = { 0, 0, 0, 0 };
    if (!SystemParametersInfoA(SPI_GETWORKAREA, 0, &work, 0)) {
        work.right = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    RECT windowRect;
    GetWindowRect(hWnd, &windowRect);
    int w = windowRect.right - windowRect.left;
    int h = windowRect.bottom - windowRect.top;
    const int workW = work.right - work.left;
    const int workH = work.bottom - work.top;
    if (workW > 0 && w > workW) w = workW;
    if (workH > 0 && h > workH) h = workH;
    if (w != windowRect.right - windowRect.left ||
        h != windowRect.bottom - windowRect.top) {
        SetWindowPos(hWnd, NULL, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER);
    }
    SetWindowPos(hWnd, NULL,
        work.left + (workW - w) / 2,
        work.top + (workH - h) / 2,
        0, 0, SWP_NOSIZE | SWP_NOZORDER);

    return hWnd;
}
