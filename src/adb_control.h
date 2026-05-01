#pragma once

#include <string>
#include <vector>

class ADBControl {
public:
    bool init();
    bool setupAudioSource();
    void cleanup();

    bool isADBExists() const;
    std::vector<std::string> getDevices() const;
    bool startApp();
    bool createForward(int port, const std::string& remoteSocket);
    bool removeForward(int port);

private:
    std::string runCommand(const std::string& cmd) const;
    std::string m_serial;
};
