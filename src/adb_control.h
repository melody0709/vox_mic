#pragma once

#include <string>
#include <vector>

std::string runCommandNoWindow(const std::string& cmdLine, unsigned long timeoutMs = 5000);

class ADBControl {
public:
    bool init(const std::string& preferredSerial = "");
    bool setupAudioSource(const std::string& androidComponent = "fr.dzx.audiosource/.MainActivity",
                          const std::string& androidSocket = "audiosource",
                          int port = 27183,
                          bool ns = true, bool aec = true, bool agc = true);
    void cleanup(int port = 27183);

    bool isADBExists() const;
    std::vector<std::string> getDevices() const;
    bool isDeviceOnline(const std::string& preferredSerial = "") const;
    const std::string& selectedSerial() const;
    bool startApp(const std::string& component, bool ns, bool aec, bool agc);
    bool createForward(int port, const std::string& remoteSocket);
    bool removeForward(int port);
    bool refreshForward(int port, const std::string& remoteSocket);
    bool verifyForward(int port, const std::string& remoteSocket) const;

private:
    std::string m_serial;
};
