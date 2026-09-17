#include "socket_client.h"
#include <cstdio>

SocketClient::SocketClient() {}

SocketClient::~SocketClient() {
    disconnect();
    cleanup();
}

bool SocketClient::init() {
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        printf("WSAStartup failed: %d\n", result);
        return false;
    }
    m_initialized = true;
    return true;
}

void SocketClient::cleanup() {
    if (m_initialized) {
        WSACleanup();
        m_initialized = false;
    }
}

bool SocketClient::connect(const std::string& host, int port) {
    disconnect();

    m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_socket == INVALID_SOCKET) {
        printf("socket() failed: %d\n", WSAGetLastError());
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(port));
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (::connect(m_socket, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        printf("connect() failed: %d\n", WSAGetLastError());
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
        return false;
    }

    // Bound every blocking read. Without this, a peer that stalls part-way
    // through a block leaves recv() parked indefinitely; the bridge loop can
    // then never observe g_appState.running going false, and main()'s bridge.join()
    // hangs for good. A timeout turns that into an ordinary recovery path.
    DWORD recvTimeoutMs = static_cast<DWORD>(RECV_TIMEOUT_MS);
    if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO,
            reinterpret_cast<const char*>(&recvTimeoutMs),
            sizeof(recvTimeoutMs)) == SOCKET_ERROR) {
        printf("setsockopt(SO_RCVTIMEO) failed: %d\n", WSAGetLastError());
    }

    return true;
}

void SocketClient::disconnect() {
    if (m_socket != INVALID_SOCKET) {
        shutdown(m_socket, SD_BOTH);
        closesocket(m_socket);
        m_socket = INVALID_SOCKET;
    }
}

bool SocketClient::isConnected() const {
    return m_socket != INVALID_SOCKET;
}

int SocketClient::recvExact(uint8_t* buffer, int size) {
    int total = 0;
    while (total < size) {
        int n = recv(m_socket, (char*)(buffer + total), size - total, 0);
        if (n > 0) {
            total += n;
            continue;
        }
        if (n == 0) return 0;
        const int err = WSAGetLastError();
        if (err == WSAETIMEDOUT) return RECV_TIMEOUT;
        return RECV_ERROR;
    }
    return total;
}

bool SocketClient::waitForData(int timeoutMs) {
    if (m_socket == INVALID_SOCKET) return false;
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(m_socket, &fds);
    timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    return select(0, &fds, NULL, NULL, &tv) > 0;
}
