#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <cstdio>
#include <vector>

#include "settings_control_ids.h"
#include "settings_fields.h"
#include "settings_layout.h"

int main() {
    INITCOMMONCONTROLSEX icc = { sizeof(INITCOMMONCONTROLSEX), ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    WNDCLASSEXA wc = {};
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "TestSettingsClass";
    RegisterClassExA(&wc);

    HWND hWnd = CreateWindowExA(
        WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT,
        "TestSettingsClass",
        "Test",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        100, 100, 600, 700,
        NULL, NULL, GetModuleHandleA(NULL), NULL);

    settings::Layout layout;
    layout.build(hWnd, GetModuleHandleA(NULL));
    layout.showPage(0);

    HWND hTab = layout.control(IDC_TAB_MAIN);
    std::printf("Tab control HWND: %p\n", (void*)hTab);

    std::vector<int> expectedPage0 = {
        IDC_COMBO_DEVICE, IDC_BTN_REFRESH, IDC_HOST_EDIT, IDC_PORT_EDIT,
        IDC_COMBO_ANDROID_APP, IDC_TRACKBAR_GAIN, IDC_CHECK_NS,
        IDC_CHECK_AEC, IDC_CHECK_AGC, IDC_CHECK_DEBUG, IDC_CHECK_STARTUP,
        IDC_BTN_RESET, IDC_BTN_CANCEL, IDC_BTN_APPLY, IDC_BTN_OK, IDC_TAB_MAIN
    };

    std::vector<int> actualPage0;
    HWND cur = hTab;
    for (int i = 0; i < 30; ++i) {
        HWND next = GetNextDlgTabItem(hWnd, cur, FALSE);
        actualPage0.push_back(GetDlgCtrlID(next));
        if (next == hTab) break;
        cur = next;
    }

    if (actualPage0 != expectedPage0) {
        std::fprintf(stderr, "FAIL: Page 0 tab order mismatch\n");
        return 1;
    }

    layout.showPage(1);
    std::vector<int> expectedPage1 = {
        IDC_CHECK_NR, IDC_COMBO_NR_BACKEND, IDC_TRACKBAR_NRSTR,
        IDC_CHECK_EQ, IDC_TRACKBAR_PRES, IDC_TRACKBAR_BASS, IDC_CHECK_COMP,
        IDC_BTN_RESET, IDC_BTN_CANCEL, IDC_BTN_APPLY, IDC_BTN_OK, IDC_TAB_MAIN
    };

    std::vector<int> actualPage1;
    cur = hTab;
    for (int i = 0; i < 30; ++i) {
        HWND next = GetNextDlgTabItem(hWnd, cur, FALSE);
        actualPage1.push_back(GetDlgCtrlID(next));
        if (next == hTab) break;
        cur = next;
    }

    if (actualPage1 != expectedPage1) {
        std::fprintf(stderr, "FAIL: Page 1 tab order mismatch\n");
        return 1;
    }

    std::printf("PASS: Settings tab order verified for both pages\n");
    DestroyWindow(hWnd);
    return 0;
}
