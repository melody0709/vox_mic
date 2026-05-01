#include "config.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#define REG_KEY "Software\\AudioSourceWin"

Config Config::load() {
    Config cfg;
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char buffer[256];
        DWORD size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "Serial", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            cfg.serial = buffer;
        }
        size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "Host", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            cfg.host = buffer;
        }
        DWORD portVal;
        size = sizeof(portVal);
        if (RegQueryValueExA(hKey, "Port", NULL, NULL, (LPBYTE)&portVal, &size) == ERROR_SUCCESS) {
            cfg.port = (int)portVal;
        }
        size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "AndroidSocket", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            cfg.androidSocket = buffer;
        }
        size = sizeof(buffer);
        if (RegQueryValueExA(hKey, "AndroidComponent", NULL, NULL, (LPBYTE)buffer, &size) == ERROR_SUCCESS) {
            cfg.androidComponent = buffer;
        }
        DWORD presetVal;
        size = sizeof(presetVal);
        if (RegQueryValueExA(hKey, "AndroidAppPreset", NULL, NULL, (LPBYTE)&presetVal, &size) == ERROR_SUCCESS) {
            cfg.androidAppPreset = (int)presetVal;
        }
        DWORD gainRaw;
        size = sizeof(gainRaw);
        if (RegQueryValueExA(hKey, "GainPercent", NULL, NULL, (LPBYTE)&gainRaw, &size) == ERROR_SUCCESS) {
            cfg.gain = (float)gainRaw / 100.0f;
            if (cfg.gain < 0.25f) cfg.gain = 0.25f;
            if (cfg.gain > 4.0f) cfg.gain = 4.0f;
        }
        DWORD flagVal;
        size = sizeof(flagVal);
        if (RegQueryValueExA(hKey, "NsEnabled", NULL, NULL, (LPBYTE)&flagVal, &size) == ERROR_SUCCESS) {
            cfg.nsEnabled = (flagVal != 0);
        }
        size = sizeof(flagVal);
        if (RegQueryValueExA(hKey, "AecEnabled", NULL, NULL, (LPBYTE)&flagVal, &size) == ERROR_SUCCESS) {
            cfg.aecEnabled = (flagVal != 0);
        }
        size = sizeof(flagVal);
        if (RegQueryValueExA(hKey, "AgcEnabled", NULL, NULL, (LPBYTE)&flagVal, &size) == ERROR_SUCCESS) {
            cfg.agcEnabled = (flagVal != 0);
        }
        RegCloseKey(hKey);
    }
    return cfg;
}

void Config::save() const {
    HKEY hKey;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL,
        0, KEY_WRITE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
        RegSetValueExA(hKey, "Serial", 0, REG_SZ,
            (const BYTE*)serial.c_str(), (DWORD)serial.size() + 1);
        RegSetValueExA(hKey, "Host", 0, REG_SZ,
            (const BYTE*)host.c_str(), (DWORD)host.size() + 1);
        DWORD portVal = (DWORD)port;
        RegSetValueExA(hKey, "Port", 0, REG_DWORD,
            (const BYTE*)&portVal, sizeof(portVal));
        RegSetValueExA(hKey, "AndroidSocket", 0, REG_SZ,
            (const BYTE*)androidSocket.c_str(), (DWORD)androidSocket.size() + 1);
        RegSetValueExA(hKey, "AndroidComponent", 0, REG_SZ,
            (const BYTE*)androidComponent.c_str(), (DWORD)androidComponent.size() + 1);
        DWORD presetVal = (DWORD)androidAppPreset;
        RegSetValueExA(hKey, "AndroidAppPreset", 0, REG_DWORD,
            (const BYTE*)&presetVal, sizeof(presetVal));
        DWORD gainRaw = (DWORD)(gain * 100.0f);
        RegSetValueExA(hKey, "GainPercent", 0, REG_DWORD,
            (const BYTE*)&gainRaw, sizeof(gainRaw));
        DWORD nsVal = nsEnabled ? 1 : 0;
        RegSetValueExA(hKey, "NsEnabled", 0, REG_DWORD, (const BYTE*)&nsVal, sizeof(nsVal));
        DWORD aecVal = aecEnabled ? 1 : 0;
        RegSetValueExA(hKey, "AecEnabled", 0, REG_DWORD, (const BYTE*)&aecVal, sizeof(aecVal));
        DWORD agcVal = agcEnabled ? 1 : 0;
        RegSetValueExA(hKey, "AgcEnabled", 0, REG_DWORD, (const BYTE*)&agcVal, sizeof(agcVal));
        RegCloseKey(hKey);
    }
}
