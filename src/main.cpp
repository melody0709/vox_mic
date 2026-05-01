#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include "wasapi_output.h"
#include "socket_client.h"
#include "adb_control.h"
#include "tray_icon.h"
#include "config.h"
#include "settings_dialog.h"
#include "mic_usage_monitor.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 27183
#define STATS_INTERVAL_MS 5000
#define WINDOW_CLASS "AudioSourceWinClass"

static Config g_config;
static std::atomic<bool> g_running{true};
static std::atomic<bool> g_bridgeActive{true};
static std::atomic<bool> g_streaming{false};
std::atomic<float> g_gain{1.5f};
std::atomic<bool> g_eqEnabled{true};
std::atomic<float> g_eqPresence{3.0f};
std::atomic<float> g_eqBassCut{-3.0f};
std::atomic<bool> g_compressorEnabled{true};
std::atomic<bool> g_nrEnabled{true};
static std::atomic<bool> g_micRequested{false};
static std::atomic<bool> g_micStreaming{false};
static std::atomic<uint64_t> g_micOnTick{0};
static MicUsageMonitor g_micMonitor;
static std::thread g_monitorThread;
static WASAPIOutput* g_wasapiOutput{nullptr};
static TrayIcon* g_trayIcon{nullptr};
static HINSTANCE g_hInstance{nullptr};

static void syncDspAtomsFromConfig() {
    g_gain.store(g_config.gain, std::memory_order_relaxed);
    g_eqEnabled.store(g_config.eqEnabled, std::memory_order_relaxed);
    g_eqPresence.store(g_config.eqPresence, std::memory_order_relaxed);
    g_eqBassCut.store(g_config.eqBassCut, std::memory_order_relaxed);
    g_compressorEnabled.store(g_config.compressorEnabled, std::memory_order_relaxed);
    g_nrEnabled.store(g_config.nrEnabled, std::memory_order_relaxed);
}

VOID CALLBACK statsTimerProc(HWND hwnd, UINT, UINT_PTR, DWORD) {
    if (!g_wasapiOutput) return;
    printf("[Stats] recv=%d drop=%d underrun=%d queue=%zu proc=%.0fus lat=%.1fms\n",
        g_wasapiOutput->receivedBlocks.load(),
        g_wasapiOutput->droppedBlocks.load(),
        g_wasapiOutput->underruns.load(),
        g_wasapiOutput->getRingBuffer()->sizeBlocks(BLOCK_SIZE),
        g_wasapiOutput->procUsEma.load(),
        g_wasapiOutput->estLatencyMs.load());
    fflush(stdout);
}

void micMonitorThread() {
    bool lastState = false;
    while (g_running.load(std::memory_order_relaxed)) {
        bool active = g_micMonitor.isCaptureActive();
        if (active != lastState) {
            printf("[Monitor] mic=%s\n", active ? "ON" : "OFF");
            fflush(stdout);
            lastState = active;
            if (active) g_micOnTick.store(GetTickCount64(), std::memory_order_relaxed);
        }
        g_micRequested.store(active, std::memory_order_relaxed);
        Sleep(100);
    }
}

void audioBridgeThread() {
    ADBControl adb;
    SocketClient socketClient;

    if (!socketClient.init()) {
        printf("ERROR: Failed to initialize Winsock\n");
        return;
    }

    std::string serial = g_config.serial;
    std::string host = g_config.host;
    int port = g_config.port;
    std::string androidComponent = g_config.androidComponent;
    std::string androidSocket = g_config.androidSocket;
    bool ns = g_config.nsEnabled;
    bool aec = g_config.aecEnabled;
    bool agc = g_config.agcEnabled;
    syncDspAtomsFromConfig();

    while (g_running.load() && !g_bridgeActive.load()) {
        Sleep(200);
    }

    if (!g_running.load()) return;

    while (g_running.load()) {
        if (!adb.init(serial)) {
            Sleep(2000);
            continue;
        }
        if (!adb.setupAudioSource(androidComponent, androidSocket, ns, aec, agc)) {
            Sleep(2000);
            continue;
        }
        break;
    }

    if (!g_running.load()) { adb.cleanup(); return; }
    printf("ADB ready, entering Always Hot mode\n");
    fflush(stdout);

    while (g_running.load()) {
        if (!g_bridgeActive.load()) {
            Sleep(200);
            continue;
        }

        if (!socketClient.isConnected()) {
            if (!socketClient.connect(host, port)) {
                Sleep(200);
                continue;
            }
            printf("Socket connected\n");
            fflush(stdout);
            g_streaming.store(true);
            g_micStreaming.store(true);
            if (g_trayIcon) g_trayIcon->updateIcon(true, true);
        }

        uint8_t buffer[BLOCK_SIZE];
        int idleCount = 0;
        int staleCount = 0;
        bool wasIdle = true;

        while (g_running.load() && g_bridgeActive.load()) {
            if (!socketClient.isConnected()) break;

            if (!socketClient.waitForData(100)) {
                staleCount++;
                if (staleCount > 90) {
                    printf("Socket stalled (9s), reconnecting\n");
                    fflush(stdout);
                    socketClient.disconnect();
                    g_streaming.store(false);
                    g_micStreaming.store(false);
                    if (g_trayIcon) g_trayIcon->updateIcon(false, false);
                    break;
                }
                continue;
            }
            staleCount = 0;

            int received = socketClient.recvExact(buffer, BLOCK_SIZE);
            if (received <= 0) {
                printf("Socket lost, reconnecting\n");
                fflush(stdout);
                socketClient.disconnect();
                g_streaming.store(false);
                g_micStreaming.store(false);
                if (g_trayIcon) g_trayIcon->updateIcon(false, false);
                break;
            }

            if (!g_micRequested.load(std::memory_order_relaxed)) {
                g_micStreaming.store(false);
                idleCount++;
                wasIdle = true;
                if (idleCount > 50) {
                    g_wasapiOutput->getRingBuffer()->reset();
                    idleCount = 0;
                }
                continue;
            }

            idleCount = 0;
            g_micStreaming.store(true);

            if (wasIdle) {
                uint64_t t = g_micOnTick.load(std::memory_order_relaxed);
                if (t) {
                    uint64_t now = GetTickCount64();
                    printf("[DetectLatency] %llums\n", (unsigned long long)(now - t));
                    fflush(stdout);
                }
                wasIdle = false;
            }

            size_t queueSize = g_wasapiOutput->getRingBuffer()->sizeBlocks(BLOCK_SIZE);
            if (queueSize > 3) {
                size_t toDrop = queueSize - 2;
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
        g_micStreaming.store(false);
        if (g_trayIcon) {
            g_trayIcon->updateIcon(false, false);
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
        case ID_MENU_START:
            g_bridgeActive.store(true);
            printf("Bridge started\n");
            fflush(stdout);
            break;

        case ID_MENU_STOP:
            g_bridgeActive.store(false);
            printf("Bridge stopped\n");
            fflush(stdout);
            break;

        case ID_MENU_SETTINGS: {
            Config newConfig = g_config;
            if (showSettingsDialog(g_hInstance, hWnd, newConfig)) {
                g_config = newConfig;
                syncDspAtomsFromConfig();
                printf("Settings saved, will take effect on next reconnect\n");
                fflush(stdout);
            }
            break;
        }

        case ID_MENU_EXIT:
            if (g_trayIcon) g_trayIcon->destroy();
            g_running.store(false);
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

int main(int argc, char* argv[]) {
    printf("AudioSource Win (Raw WASAPI) - Android microphone to Windows\n");
    printf("=============================================================\n\n");
    fflush(stdout);

    g_config = Config::load();
    syncDspAtomsFromConfig();

    bool listDevices = false;
    std::string host = g_config.host;
    int port = g_config.port;
    std::string serial = g_config.serial;

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

    if (!serial.empty()) {
        printf("Using device: %s\n", serial.c_str());
    }

    WASAPIOutput wasapiOutput;
    g_wasapiOutput = &wasapiOutput;

    if (!wasapiOutput.init(listDevices)) {
        printf("ERROR: Failed to initialize WASAPI\n");
        return 1;
    }
    if (listDevices) return 0;

    HINSTANCE hInstance = GetModuleHandle(NULL);
    g_hInstance = hInstance;
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

    SetTimer(hWnd, 1, STATS_INTERVAL_MS, statsTimerProc);

    printf("\nStarting audio bridge...\n");
    printf("Right-click tray icon for menu.\n\n");
    fflush(stdout);

    std::thread bridge(audioBridgeThread);

    if (g_micMonitor.init()) {
        g_monitorThread = std::thread(micMonitorThread);
    }

    wasapiOutput.start();

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    printf("\nShutting down...\n");
    fflush(stdout);

    g_bridgeActive.store(false);
    g_running.store(false);
    if (bridge.joinable()) bridge.join();
    if (g_monitorThread.joinable()) g_monitorThread.join();
    g_micMonitor.shutdown();
    wasapiOutput.stop();

    KillTimer(hWnd, 1);

    printf("Done.\n");
    fflush(stdout);
    return 0;
}
