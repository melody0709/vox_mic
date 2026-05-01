#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <atomic>
#include <string>

#pragma comment(lib, "Ws2_32.lib")

class SocketClient {
public:
    SocketClient();
    ~SocketClient();

    bool init();
    void cleanup();

    bool connect(const std::string& host, int port);
    void disconnect();
    bool isConnected() const;

    int recvExact(uint8_t* buffer, int size);
    bool waitForData(int timeoutMs);

private:
    SOCKET m_socket{INVALID_SOCKET};
    bool m_initialized{false};
};
