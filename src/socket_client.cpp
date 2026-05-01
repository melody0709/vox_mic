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
        if (n <= 0) {
            return n;
        }
        total += n;
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
