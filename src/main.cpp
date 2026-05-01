#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include <csignal>
#include "wasapi_output.h"
#include "socket_client.h"
#include "adb_control.h"
#include "tray_icon.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 27183
#define STATS_INTERVAL_SEC 5
#define WINDOW_CLASS "AudioSourceWinClass"

static std::atomic<bool> g_running{true};
static std::atomic<bool> g_streaming{false};
static WASAPIOutput* g_wasapiOutput{nullptr};
static TrayIcon* g_trayIcon{nullptr};

void statsThread(WASAPIOutput* output) {
    while (g_running.load()) {
        Sleep(STATS_INTERVAL_SEC * 1000);
        if (!g_running.load()) break;

        printf("[Stats] recv=%d drop=%d underrun=%d queue=%zu\n",
            output->receivedBlocks.load(),
            output->droppedBlocks.load(),
            output->underruns.load(),
            output->getRingBuffer()->sizeBlocks(BLOCK_SIZE));
        fflush(stdout);
    }
}

void audioBridgeThread(const std::string& host, int port) {
    ADBControl adb;
    SocketClient socketClient;

    if (!socketClient.init()) {
        printf("ERROR: Failed to initialize Winsock\n");
        return;
    }

    while (g_running.load()) {
        if (!adb.init()) {
            Sleep(2000);
            continue;
        }

        if (!adb.setupAudioSource()) {
            Sleep(2000);
            continue;
        }

        printf("Connecting to %s:%d...\n", host.c_str(), port);
        fflush(stdout);
        if (!socketClient.connect(host, port)) {
            Sleep(1000);
            continue;
        }

        printf("Connected! Streaming audio...\n");
        fflush(stdout);
        g_streaming.store(true);
        if (g_trayIcon) {
            g_trayIcon->updateIcon(true, true);
        }

        uint8_t buffer[BLOCK_SIZE];
        while (g_running.load() && socketClient.isConnected()) {
            int received = socketClient.recvExact(buffer, BLOCK_SIZE);
            if (received <= 0) {
                printf("Socket disconnected\n");
                fflush(stdout);
                break;
            }

            // Keep buffer low to minimize latency
            size_t queueSize = g_wasapiOutput->getRingBuffer()->sizeBlocks(BLOCK_SIZE);
            if (queueSize > 16) {
                // Drop oldest blocks to keep queue around 8
                size_t toDrop = queueSize - 8;
                uint8_t tmp[BLOCK_SIZE];
                for (size_t i = 0; i < toDrop; i++) {
                    g_wasapiOutput->getRingBuffer()->pop(tmp, BLOCK_SIZE);
                }
                g_wasapiOutput->droppedBlocks.fetch_add((int)toDrop, std::memory_order_relaxed);
            }

            if (!g_wasapiOutput->getRingBuffer()->push(buffer, BLOCK_SIZE)) {
                g_wasapiOutput->droppedBlocks.fetch_add(1, std::memory_order_relaxed);
            }
            g_wasapiOutput->receivedBlocks.fetch_add(1, std::memory_order_relaxed);
        }

        socketClient.disconnect();
        g_streaming.store(false);
        if (g_trayIcon) {
            g_trayIcon->updateIcon(false, false);
        }

        if (g_running.load()) {
            Sleep(1000);
        }
    }

    socketClient.disconnect();
    adb.cleanup();
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
                if (g_trayIcon) g_trayIcon->showMenu(hWnd);
            }
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case ID_MENU_EXIT:
                    g_running.store(false);
                    PostQuitMessage(0);
                    break;
            }
            return 0;
        case WM_DESTROY:
            if (g_trayIcon) g_trayIcon->destroy();
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int main(int argc, char* argv[]) {
    printf("AudioSource Win (Raw WASAPI) - Android microphone to Windows\n");
    printf("=============================================================\n\n");
    fflush(stdout);

    bool listDevices = false;
    std::string host = DEFAULT_HOST;
    int port = DEFAULT_PORT;
    std::string serial;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--list-devices") {
            listDevices = true;
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (arg == "--serial" && i + 1 < argc) {
            serial = argv[++i];
        } else if (arg == "--help") {
            printf("Usage: %s [options]\n", argv[0]);
            printf("Options:\n");
            printf("  --list-devices    List audio devices and exit\n");
            printf("  --host <host>     Socket host (default: %s)\n", DEFAULT_HOST);
            printf("  --port <port>     Socket port (default: %d)\n", DEFAULT_PORT);
            printf("  --serial <serial> ADB device serial\n");
            printf("  --help            Show this help\n");
            return 0;
        }
    }

    WASAPIOutput wasapiOutput;
    g_wasapiOutput = &wasapiOutput;

    if (!wasapiOutput.init(listDevices)) {
        printf("ERROR: Failed to initialize WASAPI\n");
        return 1;
    }
    if (listDevices) return 0;

    if (!serial.empty()) {
        printf("Using specified device serial: %s\n", serial.c_str());
    }

    // Create hidden window for tray icon
    HINSTANCE hInstance = GetModuleHandle(NULL);
    WNDCLASSA wc{};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = WINDOW_CLASS;
    RegisterClassA(&wc);

    HWND hWnd = CreateWindowExA(0, WINDOW_CLASS, "AudioSource Win",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    TrayIcon trayIcon;
    g_trayIcon = &trayIcon;
    if (hWnd) {
        trayIcon.create(hInstance, hWnd);
    }

    printf("\nStarting audio bridge...\n");
    printf("Right-click tray icon to exit.\n\n");
    fflush(stdout);

    // Start bridge first to fill buffer, then start audio
    std::thread stats(statsThread, &wasapiOutput);
    std::thread bridge(audioBridgeThread, host, port);

    // Wait for buffer to fill before starting audio render
    while (g_running.load() && wasapiOutput.getRingBuffer()->sizeBlocks(BLOCK_SIZE) < 3) {
        Sleep(50);
    }
    if (g_running.load()) {
        printf("Buffer filled, starting audio render\n");
        fflush(stdout);
    }

    wasapiOutput.start();

    // Message loop (needed for tray icon)
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    printf("\nShutting down...\n");
    fflush(stdout);

    g_running.store(false);
    if (bridge.joinable()) bridge.join();
    if (stats.joinable()) stats.join();
    wasapiOutput.stop();

    printf("Done.\n");
    fflush(stdout);
    return 0;
}
