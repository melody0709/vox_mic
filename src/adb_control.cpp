#include "adb_control.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <stdexcept>
#include <sstream>

std::string runCommandNoWindow(const std::string& cmdLine) {
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return "";

    SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError = hWrite;

    PROCESS_INFORMATION pi = {};
    char* cmd = _strdup(cmdLine.c_str());

    BOOL ok = CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
                             CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(cmd);
    CloseHandle(hWrite);

    if (!ok) {
        CloseHandle(hRead);
        return "";
    }

    std::string result;
    char buf[512];
    DWORD bytesRead;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &bytesRead, NULL) && bytesRead > 0) {
        buf[bytesRead] = '\0';
        result += buf;
    }
    CloseHandle(hRead);
    WaitForSingleObject(pi.hProcess, 5000);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

bool ADBControl::isADBExists() const {
    std::string result = runCommandNoWindow("adb version");
    return result.find("Android Debug Bridge") != std::string::npos;
}

std::vector<std::string> ADBControl::getDevices() const {
    std::vector<std::string> devices;
    std::string result = runCommandNoWindow("adb devices");
    std::istringstream stream(result);
    std::string line;
    bool first = true;
    while (std::getline(stream, line)) {
        if (first) { first = false; continue; }
        if (line.empty()) continue;
        size_t tabPos = line.find('\t');
        if (tabPos != std::string::npos) {
            std::string serial = line.substr(0, tabPos);
            std::string status = line.substr(tabPos + 1);
            if (status.find("device") != std::string::npos) {
                devices.push_back(serial);
            }
        }
    }
    return devices;
}

bool ADBControl::startApp(const std::string& component, bool ns, bool aec, bool agc) {
    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " shell am start -n " + component
        + " --ez ns_enabled " + (ns ? "true" : "false")
        + " --ez aec_enabled " + (aec ? "true" : "false")
        + " --ez agc_enabled " + (agc ? "true" : "false");
    std::string result = runCommandNoWindow(cmd);
    return result.find("Error") == std::string::npos;
}

bool ADBControl::createForward(int port, const std::string& remoteSocket) {
    removeForward(port);

    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " forward tcp:" + std::to_string(port) + " " + remoteSocket;
    runCommandNoWindow(cmd);
    return true;
}

bool ADBControl::removeForward(int port) {
    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " forward --remove tcp:" + std::to_string(port);
    runCommandNoWindow(cmd);
    return true;
}

bool ADBControl::refreshForward(int port, const std::string& remoteSocket) {
    removeForward(port);
    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " forward tcp:" + std::to_string(port) + " " + remoteSocket;
    runCommandNoWindow(cmd);
    printf("ADB forward refreshed: tcp:%d -> %s\n", port, remoteSocket.c_str());
    fflush(stdout);
    return true;
}

bool ADBControl::init(const std::string& preferredSerial) {
    if (!isADBExists()) {
        printf("ERROR: adb not found. Install Android Platform Tools and make sure adb is in PATH.\n");
        return false;
    }

    auto devices = getDevices();
    if (devices.empty()) {
        printf("ERROR: No Android device is online. Enable USB debugging and check that adb devices shows device.\n");
        return false;
    }

    if (!preferredSerial.empty()) {
        for (const auto& d : devices) {
            if (d == preferredSerial) {
                m_serial = preferredSerial;
                printf("Device selected: %s\n", m_serial.c_str());
                return true;
            }
        }
        printf("WARNING: Preferred device %s not found, falling back to auto-detect\n",
            preferredSerial.c_str());
    }

    if (devices.size() > 1) {
        printf("WARNING: Multiple devices detected, using first: %s\n", devices[0].c_str());
        printf("Select a specific device via Settings or --serial\n");
    }

    m_serial = devices[0];
    printf("Android device online: %s\n", m_serial.c_str());
    return true;
}

bool ADBControl::setupAudioSource(const std::string& androidComponent,
                                    const std::string& androidSocket,
                                    bool ns, bool aec, bool agc) {
    if (!startApp(androidComponent, ns, aec, agc)) {
        printf("ERROR: Failed to start Android app (%s)\n", androidComponent.c_str());
        return false;
    }
    printf("Android app started (NS=%d AEC=%d AGC=%d)\n", ns, aec, agc);

    Sleep(1500);

    std::string remoteSocket = "localabstract:" + androidSocket;
    if (!createForward(27183, remoteSocket)) {
        printf("ERROR: Failed to create ADB forward\n");
        return false;
    }
    printf("ADB forward ready: tcp:27183 -> %s\n", remoteSocket.c_str());

    return true;
}

void ADBControl::cleanup() {
    removeForward(27183);
    printf("ADB forward removed\n");
}
