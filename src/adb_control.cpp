#include "adb_control.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <array>
#include <sstream>

std::string ADBControl::runCommand(const std::string& cmd) const {
    std::array<char, 256> buffer;
    std::string result;
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(cmd.c_str(), "r"), _pclose);
    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

bool ADBControl::isADBExists() const {
    try {
        std::string result = runCommand("adb version");
        return result.find("Android Debug Bridge") != std::string::npos;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> ADBControl::getDevices() const {
    std::vector<std::string> devices;
    try {
        std::string result = runCommand("adb devices");
        std::istringstream stream(result);
        std::string line;
        bool first = true;
        while (std::getline(stream, line)) {
            if (first) {
                first = false;
                continue;
            }
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
    } catch (...) {}
    return devices;
}

bool ADBControl::startApp() {
    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " shell am start -n fr.dzx.audiosource/.MainActivity";
    try {
        std::string result = runCommand(cmd);
        return result.find("Error") == std::string::npos;
    } catch (...) {
        return false;
    }
}

bool ADBControl::createForward(int port, const std::string& remoteSocket) {
    removeForward(port);

    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " forward tcp:" + std::to_string(port) + " " + remoteSocket;
    try {
        runCommand(cmd);
        return true;
    } catch (...) {
        return false;
    }
}

bool ADBControl::removeForward(int port) {
    std::string cmd = "adb";
    if (!m_serial.empty()) {
        cmd += " -s " + m_serial;
    }
    cmd += " forward --remove tcp:" + std::to_string(port);
    try {
        runCommand(cmd);
        return true;
    } catch (...) {
        return false;
    }
}

bool ADBControl::init() {
    if (!isADBExists()) {
        printf("ERROR: adb not found. Install Android Platform Tools and make sure adb is in PATH.\n");
        return false;
    }

    auto devices = getDevices();
    if (devices.empty()) {
        printf("ERROR: No Android device is online. Enable USB debugging and check that adb devices shows device.\n");
        return false;
    }

    if (devices.size() > 1) {
        printf("WARNING: Multiple devices detected, using first: %s\n", devices[0].c_str());
        printf("Use --serial to specify device if needed.\n");
    }

    m_serial = devices[0];
    printf("Android device online: %s\n", m_serial.c_str());
    return true;
}

bool ADBControl::setupAudioSource() {
    if (!startApp()) {
        printf("ERROR: Failed to start AudioSource app\n");
        return false;
    }
    printf("AudioSource app started\n");

    Sleep(1500);

    if (!createForward(27183, "localabstract:audiosource")) {
        printf("ERROR: Failed to create ADB forward\n");
        return false;
    }
    printf("ADB forward ready: tcp:27183 -> localabstract:audiosource\n");

    return true;
}

void ADBControl::cleanup() {
    removeForward(27183);
    printf("ADB forward removed\n");
}
