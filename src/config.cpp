#include "config.h"
#include "runtime_paths.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>

#define INI_SECTION "VoxMic"

static std::string getIniPath() {
    std::wstring wide = runtime_paths::ConfigPath();
    if (wide.empty()) {
        char exePath[MAX_PATH];
        GetModuleFileNameA(NULL, exePath, MAX_PATH);
        std::string path(exePath);
        size_t pos = path.find_last_of("\\/");
        if (pos != std::string::npos) path = path.substr(0, pos + 1);
        path += "config.ini";
        return path;
    }
    int len = WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string narrow(len > 0 ? len : 1, '\0');
    if (len > 0) WideCharToMultiByte(CP_ACP, 0, wide.c_str(), -1, &narrow[0], len, nullptr, nullptr);
    if (!narrow.empty() && narrow.back() == '\0') narrow.pop_back();
    return narrow;
}

static std::string readIniString(const char* path, const char* key, const char* def) {
    char buf[256];
    GetPrivateProfileStringA(INI_SECTION, key, def, buf, sizeof(buf), path);
    return buf;
}

static int readIniInt(const char* path, const char* key, int def) {
    return (int)GetPrivateProfileIntA(INI_SECTION, key, def, path);
}

static float readIniFloat(const char* path, const char* key, float def) {
    char buf[64];
    char defStr[64];
    snprintf(defStr, sizeof(defStr), "%.2f", def);
    GetPrivateProfileStringA(INI_SECTION, key, defStr, buf, sizeof(buf), path);
    return (float)atof(buf);
}

static void writeIniString(const char* path, const char* key, const char* val) {
    WritePrivateProfileStringA(INI_SECTION, key, val, path);
}

static void writeIniInt(const char* path, const char* key, int val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", val);
    WritePrivateProfileStringA(INI_SECTION, key, buf, path);
}

static void writeIniFloat(const char* path, const char* key, float val) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%.2f", val);
    WritePrivateProfileStringA(INI_SECTION, key, buf, path);
}

Config Config::load() {
    Config cfg;
    std::string path = getIniPath();

    cfg.serial = readIniString(path.c_str(), "Serial", "");
    cfg.host = readIniString(path.c_str(), "Host", "127.0.0.1");
    cfg.port = readIniInt(path.c_str(), "Port", 27183);
    cfg.androidSocket = readIniString(path.c_str(), "AndroidSocket", "audiosource");
    cfg.androidComponent = readIniString(path.c_str(), "AndroidComponent", "com.voxmic.source/.MainActivity");
    cfg.androidAppPreset = readIniInt(path.c_str(), "AndroidAppPreset", 1);
    cfg.gain = readIniFloat(path.c_str(), "Gain", 1.35f);
    if (cfg.gain < 0.25f) cfg.gain = 0.25f;
    if (cfg.gain > 4.0f) cfg.gain = 4.0f;
    cfg.nsEnabled = readIniInt(path.c_str(), "NsEnabled", 0) != 0;
    cfg.aecEnabled = readIniInt(path.c_str(), "AecEnabled", 1) != 0;
    cfg.agcEnabled = readIniInt(path.c_str(), "AgcEnabled", 0) != 0;
    cfg.eqEnabled = readIniInt(path.c_str(), "EqEnabled", 1) != 0;
    cfg.eqPresence = readIniFloat(path.c_str(), "EqPresence", 3.0f);
    if (cfg.eqPresence < 0.0f) cfg.eqPresence = 0.0f;
    if (cfg.eqPresence > 8.0f) cfg.eqPresence = 8.0f;
    cfg.eqBassCut = readIniFloat(path.c_str(), "EqBassCut", -3.0f);
    if (cfg.eqBassCut < -6.0f) cfg.eqBassCut = -6.0f;
    if (cfg.eqBassCut > 0.0f) cfg.eqBassCut = 0.0f;
    cfg.compressorEnabled = readIniInt(path.c_str(), "CompressorEnabled", 1) != 0;
    cfg.nrEnabled = readIniInt(path.c_str(), "NrEnabled", 1) != 0;
    cfg.nrStrength = readIniFloat(path.c_str(), "NrStrength", 0.6f);
    if (cfg.nrStrength < 0.3f) cfg.nrStrength = 0.3f;
    if (cfg.nrStrength > 0.95f) cfg.nrStrength = 0.95f;
    cfg.debugConsole = readIniInt(path.c_str(), "DebugConsole", 1) != 0;
    cfg.demandMode = readIniInt(path.c_str(), "DemandMode", 1) != 0;
    cfg.alwaysHot = readIniInt(path.c_str(), "AlwaysHot", 0) != 0;

    return cfg;
}

void Config::save() const {
    std::string path = getIniPath();

    writeIniString(path.c_str(), "Serial", serial.c_str());
    writeIniString(path.c_str(), "Host", host.c_str());
    writeIniInt(path.c_str(), "Port", port);
    writeIniString(path.c_str(), "AndroidSocket", androidSocket.c_str());
    writeIniString(path.c_str(), "AndroidComponent", androidComponent.c_str());
    writeIniInt(path.c_str(), "AndroidAppPreset", androidAppPreset);
    writeIniFloat(path.c_str(), "Gain", gain);
    writeIniInt(path.c_str(), "NsEnabled", nsEnabled ? 1 : 0);
    writeIniInt(path.c_str(), "AecEnabled", aecEnabled ? 1 : 0);
    writeIniInt(path.c_str(), "AgcEnabled", agcEnabled ? 1 : 0);
    writeIniInt(path.c_str(), "EqEnabled", eqEnabled ? 1 : 0);
    writeIniFloat(path.c_str(), "EqPresence", eqPresence);
    writeIniFloat(path.c_str(), "EqBassCut", eqBassCut);
    writeIniInt(path.c_str(), "CompressorEnabled", compressorEnabled ? 1 : 0);
    writeIniInt(path.c_str(), "NrEnabled", nrEnabled ? 1 : 0);
    writeIniFloat(path.c_str(), "NrStrength", nrStrength);
    writeIniInt(path.c_str(), "DebugConsole", debugConsole ? 1 : 0);
    writeIniInt(path.c_str(), "DemandMode", demandMode ? 1 : 0);
    writeIniInt(path.c_str(), "AlwaysHot", alwaysHot ? 1 : 0);
}
