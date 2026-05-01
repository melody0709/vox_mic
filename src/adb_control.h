#pragma once

#include <string>
#include <vector>

class ADBControl {
public:
    bool init(const std::string& preferredSerial = "");
    bool setupAudioSource(const std::string& androidComponent = "fr.dzx.audiosource/.MainActivity",
                          const std::string& androidSocket = "audiosource",
                          bool ns = true, bool aec = true, bool agc = true);
    void cleanup();

    bool isADBExists() const;
    std::vector<std::string> getDevices() const;
    bool startApp(const std::string& component, bool ns, bool aec, bool agc);
    bool createForward(int port, const std::string& remoteSocket);
    bool removeForward(int port);

private:
    std::string runCommand(const std::string& cmd) const;
    std::string m_serial;
};
