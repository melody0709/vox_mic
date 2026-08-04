#include "adb_control.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#include <cctype>

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return (char)std::tolower(c);
    });
    return s;
}

static bool hasAdbError(const std::string& result) {
    std::string lower = toLower(result);
    return lower.find("error") != std::string::npos ||
           lower.find("offline") != std::string::npos ||
           lower.find("unauthorized") != std::string::npos ||
           lower.find("no devices") != std::string::npos ||
           lower.find("more than one device") != std::string::npos ||
           lower.find("[timeout]") != std::string::npos;
}

std::string runCommandNoWindow(const std::string& cmdLine, unsigned long timeoutMs) {
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
    DWORD startTick = GetTickCount();
    bool timedOut = false;

    while (true) {
        DWORD available = 0;
        while (PeekNamedPipe(hRead, NULL, 0, NULL, &available, NULL) && available > 0) {
            DWORD toRead = available < sizeof(buf) - 1 ? available : (DWORD)sizeof(buf) - 1;
            if (!ReadFile(hRead, buf, toRead, &bytesRead, NULL) || bytesRead == 0) break;
            buf[bytesRead] = '\0';
            result += buf;
            available = 0;
        }

        DWORD wait = WaitForSingleObject(pi.hProcess, 50);
        if (wait == WAIT_OBJECT_0) {
            while (PeekNamedPipe(hRead, NULL, 0, NULL, &available, NULL) && available > 0) {
                DWORD toRead = available < sizeof(buf) - 1 ? available : (DWORD)sizeof(buf) - 1;
                if (!ReadFile(hRead, buf, toRead, &bytesRead, NULL) || bytesRead == 0) break;
                buf[bytesRead] = '\0';
                result += buf;
                available = 0;
            }
            break;
        }

        if (timeoutMs > 0 && GetTickCount() - startTick >= timeoutMs) {
            timedOut = true;
            TerminateProcess(pi.hProcess, 1);
            WaitForSingleObject(pi.hProcess, 1000);
            break;
        }
    }

    CloseHandle(hRead);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (timedOut) {
        result += "\n[TIMEOUT]\n";
    }
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

bool ADBControl::isDeviceOnline(const std::string& preferredSerial) const {
    auto devices = getDevices();
    if (devices.empty()) return false;

    std::string target = preferredSerial;
    if (target.empty()) target = m_serial;
    if (target.empty()) return true;

    for (const auto& d : devices) {
        if (d == target) return true;
    }
    return false;
}

const std::string& ADBControl::selectedSerial() const {
    return m_serial;
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
    return !hasAdbError(result);
}

bool ADBControl::createForward(int port, const std::string& remoteSocket) {
    removeForward(port);

    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " forward tcp:" + std::to_string(port) + " " + remoteSocket;
    std::string result = runCommandNoWindow(cmd);
    if (hasAdbError(result)) {
        printf("ERROR: adb forward failed: %s\n", result.c_str());
        fflush(stdout);
        return false;
    }
    return verifyForward(port, remoteSocket);
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
    std::string result = runCommandNoWindow(cmd);
    if (hasAdbError(result)) {
        printf("ERROR: adb forward refresh failed: %s\n", result.c_str());
        fflush(stdout);
        return false;
    }
    if (!verifyForward(port, remoteSocket)) {
        printf("ERROR: ADB forward refresh could not be verified\n");
        fflush(stdout);
        return false;
    }
    printf("ADB forward refreshed: tcp:%d -> %s\n", port, remoteSocket.c_str());
    fflush(stdout);
    return true;
}

bool ADBControl::verifyForward(int port, const std::string& remoteSocket) const {
    std::string result = runCommandNoWindow("adb forward --list");
    if (hasAdbError(result)) return false;

    std::istringstream stream(result);
    std::string line;
    std::string tcp = "tcp:" + std::to_string(port);
    while (std::getline(stream, line)) {
        if (!m_serial.empty() && line.find(m_serial) == std::string::npos) continue;
        if (line.find(tcp) != std::string::npos &&
            line.find(remoteSocket) != std::string::npos) {
            return true;
        }
    }
    return false;
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
                                    int port,
                                    bool ns, bool aec, bool agc) {
    if (!startApp(androidComponent, ns, aec, agc)) {
        printf("ERROR: Failed to start Android app (%s)\n", androidComponent.c_str());
        return false;
    }
    printf("Android app started (NS=%d AEC=%d AGC=%d)\n", ns, aec, agc);

    Sleep(1500);

    std::string remoteSocket = "localabstract:" + androidSocket;
    if (!createForward(port, remoteSocket)) {
        printf("ERROR: Failed to create ADB forward\n");
        return false;
    }
    printf("ADB forward ready: tcp:%d -> %s\n", port, remoteSocket.c_str());

    return true;
}

void ADBControl::cleanup(int port) {
    removeForward(port);
    printf("ADB forward removed\n");
}
