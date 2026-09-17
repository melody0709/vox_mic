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

    // recvExact() result codes. A peer that stalls mid-block must not be able
    // to park the bridge thread forever, so the socket carries SO_RCVTIMEO and
    // a timeout is reported distinctly from a closed connection - that is what
    // makes "why did it reconnect" answerable from the log.
    static constexpr int RECV_TIMEOUT = -1;
    static constexpr int RECV_ERROR = -2;

    // How long a single recv may block. Also the upper bound on how long
    // shutdown can wait for the bridge thread to notice g_appState.running going false.
    static constexpr int RECV_TIMEOUT_MS = 500;

    bool init();
    void cleanup();

    bool connect(const std::string& host, int port);
    void disconnect();
    bool isConnected() const;

    // Reads exactly `size` bytes.
    //   > 0  bytes read (equals size on success)
    //   0    peer closed the connection
    //   RECV_TIMEOUT / RECV_ERROR otherwise (see SocketClient::RECV_ERROR)
    int recvExact(uint8_t* buffer, int size);
    bool waitForData(int timeoutMs);

private:
    SOCKET m_socket{INVALID_SOCKET};
    bool m_initialized{false};
};
