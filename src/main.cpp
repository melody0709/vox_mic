#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstdio>
#include <atomic>
#include <cstring>
#include <thread>
#include "wasapi_output.h"
#include "socket_client.h"
#include "adb_control.h"
#include "tray_icon.h"
#include "config.h"
#include "settings_dialog.h"
#include "mic_usage_monitor.h"
#include "runtime_paths.h"

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 27183
#define STATS_INTERVAL_MS 5000
#define IDLE_HEALTH_CHECK_MS 15000
#define ADB_LOST_RETRY_MIN_MS 1000
#define ADB_LOST_RETRY_MAX_MS 3000
#define ADB_LOST_LOG_MS 30000

Config g_config;
std::atomic<bool> g_running{true};
static std::atomic<bool> g_streaming{false};
std::atomic<float> g_gain{1.5f};
std::atomic<bool> g_eqEnabled{false};
std::atomic<float> g_eqPresence{3.0f};
std::atomic<float> g_eqBassCut{-3.0f};
std::atomic<bool> g_compressorEnabled{false};
std::atomic<bool> g_nrEnabled{true};
std::atomic<float> g_nrStrength{0.6f};
std::atomic<int> g_denoiseBackend{static_cast<int>(DenoiseBackendKind::Dpdfnet)};
std::atomic<uint64_t> g_denoiseResetEpoch{1};
std::atomic<bool> g_dpdfnetAvailable{false};
std::atomic<bool> g_dpdfnetDegraded{false};
std::atomic<int> g_denoiseEffectiveBackend{static_cast<int>(DenoiseBackendKind::Rnnoise)};
std::atomic<bool> g_micRequested{false};
static std::atomic<bool> g_micStreaming{false};
std::atomic<bool> g_demandMode{true};
std::atomic<bool> g_alwaysHot{true};
static std::atomic<uint64_t> g_micOnTick{0};
static MicUsageMonitor g_micMonitor;
static std::thread g_monitorThread;
static WASAPIOutput* g_wasapiOutput{nullptr};
TrayIcon* g_trayIcon{nullptr};
static HINSTANCE g_hInstance{nullptr};

enum class BridgeRecoveryState {
    Healthy,
    AdbLost,
    Recovering
};

enum BridgeStatus {
    BRIDGE_STARTING = 0,
    BRIDGE_IDLE_SLEEP,
    BRIDGE_IDLE_HOT,
    BRIDGE_STREAMING,
    BRIDGE_ADB_LOST,
    BRIDGE_RECOVERING
};

static std::atomic<int> g_bridgeStatus{BRIDGE_STARTING};

static const char* bridgeStatusName(int status) {
    switch (status) {
    case BRIDGE_IDLE_SLEEP: return "idle-sleep";
    case BRIDGE_IDLE_HOT: return "idle-hot";
    case BRIDGE_STREAMING: return "streaming";
    case BRIDGE_ADB_LOST: return "adb-lost";
    case BRIDGE_RECOVERING: return "recovering";
    case BRIDGE_STARTING:
    default:
        return "starting";
    }
}

void syncDspAtomsFromConfig(const Config& cfg) {
    g_gain.store(cfg.gain, std::memory_order_relaxed);
    g_eqEnabled.store(cfg.eqEnabled, std::memory_order_relaxed);
    g_eqPresence.store(cfg.eqPresence, std::memory_order_relaxed);
    g_eqBassCut.store(cfg.eqBassCut, std::memory_order_relaxed);
    g_compressorEnabled.store(cfg.compressorEnabled, std::memory_order_relaxed);
    g_nrEnabled.store(cfg.nrEnabled, std::memory_order_relaxed);
    g_nrStrength.store(cfg.nrStrength, std::memory_order_relaxed);
    const int backend = (_stricmp(cfg.denoiseBackend.c_str(), "dpdfnet") == 0)
        ? static_cast<int>(DenoiseBackendKind::Dpdfnet)
        : static_cast<int>(DenoiseBackendKind::Rnnoise);
    g_denoiseBackend.store(backend, std::memory_order_release);
}

void syncDspAtomsFromConfig() {
    syncDspAtomsFromConfig(g_config);
}

void requestDenoiseReset() {
    g_denoiseResetEpoch.fetch_add(1, std::memory_order_acq_rel);
}

static void setConsoleVisible(bool visible) {
    if (visible) {
        if (!GetConsoleWindow()) {
            AllocConsole();
            freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
            freopen_s((FILE**)stderr, "CONOUT$", "w", stderr);
        }
    } else {
        if (GetConsoleWindow()) {
            FreeConsole();
        }
    }
}

VOID CALLBACK statsTimerProc(HWND, UINT, UINT_PTR, DWORD) {
    if (!g_wasapiOutput) return;
    const int effectiveBackend = g_denoiseEffectiveBackend.load(
        std::memory_order_relaxed);
    const char* denoiseName = "rnnoise";
    if (effectiveBackend == static_cast<int>(DenoiseBackendKind::Dpdfnet)) {
        denoiseName = "dpdfnet";
    } else if (effectiveBackend == static_cast<int>(DenoiseBackendKind::Off)) {
        denoiseName = "off";
    }
    printf("[Stats] state=%s recv=%d drop=%d underrun=%d idleSilence=%d queue=%zu proc=%.0fus lat=%.1fms denoise=%s dpdf(avail=%d degraded=%d uf=%llu inDrop=%llu outDrop=%llu worker=%.0fus)\n",
        bridgeStatusName(g_bridgeStatus.load(std::memory_order_relaxed)),
        g_wasapiOutput->receivedBlocks.load(),
        g_wasapiOutput->droppedBlocks.load(),
        g_wasapiOutput->underruns.load(),
        g_wasapiOutput->idleSilenceBlocks.load(),
        g_wasapiOutput->getRingBuffer()->sizeBlocks(BLOCK_SIZE),
        g_wasapiOutput->procUsEma.load(),
        g_wasapiOutput->estLatencyMs.load(),
        denoiseName,
        g_dpdfnetAvailable.load(std::memory_order_relaxed) ? 1 : 0,
        g_dpdfnetDegraded.load(std::memory_order_relaxed) ? 1 : 0,
        static_cast<unsigned long long>(g_wasapiOutput->dpdfnetUnderflows()),
        static_cast<unsigned long long>(g_wasapiOutput->dpdfnetInputDrops()),
        static_cast<unsigned long long>(g_wasapiOutput->dpdfnetOutputDrops()),
        g_wasapiOutput->dpdfnetWorkerProcUsEma());
    fflush(stdout);
}

void micMonitorThread() {
    int silenceCount = 0;
    while (g_running.load(std::memory_order_relaxed)) {
        if (!g_demandMode.load(std::memory_order_relaxed)) {
            g_micRequested.store(true, std::memory_order_relaxed);
            silenceCount = 0;
            Sleep(500);
            continue;
        }

        if (!g_micRequested.load(std::memory_order_relaxed)) {
            silenceCount = 0;
            Sleep(1000);
            continue;
        }

        float peak = g_micMonitor.getCapturePeak();
        if (peak > 0.0001f) {
            silenceCount = 0;
        } else {
            silenceCount++;
            if (silenceCount >= 3) {
                g_micRequested.store(false, std::memory_order_relaxed);
                silenceCount = 0;
            }
        }

        Sleep(1000);
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
    std::string remoteSocket = "localabstract:" + androidSocket;
    bool ns = g_config.nsEnabled;
    bool aec = g_config.aecEnabled;
    bool agc = g_config.agcEnabled;
    syncDspAtomsFromConfig();

    if (!g_running.load()) return;

    while (g_running.load()) {
        if (!adb.init(serial)) {
            Sleep(2000);
            continue;
        }
        if (!adb.setupAudioSource(androidComponent, androidSocket, port, ns, aec, agc)) {
            Sleep(2000);
            continue;
        }
        break;
    }

    if (!g_running.load()) { adb.cleanup(port); return; }
    printf("ADB ready, entering Always Hot mode\n");
    printf("Settings:\n");
    printf("  Gain = %.2fx\n", g_config.gain);
    printf("  Android HW: NS=%d AEC=%d AGC=%d\n", ns, aec, agc);
    printf("  DSP: NR=%d backend=%s EQ=%d (Presence=+%.1fdB BassCut=%.1fdB) Compressor=%d\n",
           g_config.nrEnabled, g_config.denoiseBackend.c_str(),
           g_config.eqEnabled,
           g_config.eqPresence, g_config.eqBassCut,
           g_config.compressorEnabled);
    fflush(stdout);
    g_bridgeStatus.store(BRIDGE_IDLE_SLEEP, std::memory_order_relaxed);

    int quickDisconnectCount = 0;
    int connectFailCount = 0;
    bool adbReadyOnce = true;
    bool wasPreviouslyConnected = false;
    uint64_t connectTick = 0;
    BridgeRecoveryState recoveryState = BridgeRecoveryState::Healthy;
    uint64_t nextIdleHealthTick = GetTickCount64() + IDLE_HEALTH_CHECK_MS;
    uint64_t nextAdbCheckTick = 0;
    uint64_t lastAdbLostLogTick = 0;
    uint64_t adbLostDelayMs = ADB_LOST_RETRY_MIN_MS;

    auto enterAdbLost = [&](const char* reason) {
        uint64_t now = GetTickCount64();
        if (recoveryState != BridgeRecoveryState::AdbLost) {
            printf("[Bridge] ADB lost: %s\n", reason);
            fflush(stdout);
        }
        recoveryState = BridgeRecoveryState::AdbLost;
        g_bridgeStatus.store(BRIDGE_ADB_LOST, std::memory_order_relaxed);
        nextAdbCheckTick = now + adbLostDelayMs;
        lastAdbLostLogTick = now;
        socketClient.disconnect();
        requestDenoiseReset();
        g_streaming.store(false);
        g_micStreaming.store(false);
        if (g_trayIcon) g_trayIcon->updateIcon(false, false);
    };

    auto recoverAdb = [&](const char* reason) -> bool {
        printf("[Bridge] Recovering ADB: %s\n", reason);
        fflush(stdout);
        recoveryState = BridgeRecoveryState::Recovering;
        g_bridgeStatus.store(BRIDGE_RECOVERING, std::memory_order_relaxed);

        if (!adb.init(serial)) {
            enterAdbLost("adb init failed during recovery");
            return false;
        }
        if (!adb.setupAudioSource(androidComponent, androidSocket, port, ns, aec, agc)) {
            enterAdbLost("setupAudioSource failed during recovery");
            return false;
        }
        if (!adb.verifyForward(port, remoteSocket)) {
            enterAdbLost("forward verification failed during recovery");
            return false;
        }

        recoveryState = BridgeRecoveryState::Healthy;
        adbReadyOnce = true;
        connectFailCount = 0;
        quickDisconnectCount = 0;
        adbLostDelayMs = ADB_LOST_RETRY_MIN_MS;
        nextIdleHealthTick = GetTickCount64() + IDLE_HEALTH_CHECK_MS;
        printf("[Bridge] ADB recovered and forward verified\n");
        fflush(stdout);
        return true;
    };

    auto pollAdbLost = [&]() {
        uint64_t now = GetTickCount64();
        if (now < nextAdbCheckTick) return;

        if (adb.isDeviceOnline(serial)) {
            adbLostDelayMs = ADB_LOST_RETRY_MIN_MS;
            recoverAdb("device online");
            return;
        }

        if (now - lastAdbLostLogTick >= ADB_LOST_LOG_MS) {
            printf("[Bridge] waiting for ADB device...\n");
            fflush(stdout);
            lastAdbLostLogTick = now;
        }
        nextAdbCheckTick = now + adbLostDelayMs;
        if (adbLostDelayMs < ADB_LOST_RETRY_MAX_MS) {
            adbLostDelayMs += 1000;
            if (adbLostDelayMs > ADB_LOST_RETRY_MAX_MS) {
                adbLostDelayMs = ADB_LOST_RETRY_MAX_MS;
            }
        }
    };

    while (g_running.load()) {
        if (recoveryState == BridgeRecoveryState::AdbLost) {
            pollAdbLost();
            Sleep(200);
            continue;
        }

        if (!g_alwaysHot.load(std::memory_order_relaxed) &&
            g_demandMode.load(std::memory_order_relaxed) &&
            !g_micRequested.load(std::memory_order_relaxed)) {
            g_bridgeStatus.store(BRIDGE_IDLE_SLEEP, std::memory_order_relaxed);
            uint64_t now = GetTickCount64();
            if (adbReadyOnce && now >= nextIdleHealthTick) {
                nextIdleHealthTick = now + IDLE_HEALTH_CHECK_MS;
                if (!adb.isDeviceOnline(serial)) {
                    enterAdbLost("idle health check found device offline");
                } else if (!adb.verifyForward(port, remoteSocket)) {
                    printf("[Bridge] idle health check found missing forward\n");
                    fflush(stdout);
                    recoverAdb("idle forward missing");
                }
            }
            Sleep(200);
            continue;
        }

        if (!socketClient.isConnected()) {
            LARGE_INTEGER qpcFreq, t0, t1;
            QueryPerformanceFrequency(&qpcFreq);
            QueryPerformanceCounter(&t0);
            if (!socketClient.connect(host, port)) {
                connectFailCount++;
                printf("[Bridge] connect fail #%d\n", connectFailCount);
                fflush(stdout);
                if (!adb.isDeviceOnline(serial)) {
                    enterAdbLost("connect failed and ADB device is offline");
                    Sleep(200);
                    continue;
                }
                if (connectFailCount == 1 && adbReadyOnce) {
                    printf("[Bridge] first fail after connection, refreshing ADB forward\n");
                    fflush(stdout);
                    if (!adb.refreshForward(port, remoteSocket)) {
                        recoverAdb("forward refresh failed");
                    }
                }
                if (connectFailCount >= 3) {
                    printf("[Bridge] 3 consecutive connect fails, full reset\n");
                    fflush(stdout);
                    recoverAdb("3 consecutive connect fails");
                    connectFailCount = 0;
                }
                Sleep(200);
                continue;
            }
            connectFailCount = 0;
            wasPreviouslyConnected = true;
            recoveryState = BridgeRecoveryState::Healthy;
            nextIdleHealthTick = GetTickCount64() + IDLE_HEALTH_CHECK_MS;
            g_bridgeStatus.store(BRIDGE_STREAMING, std::memory_order_relaxed);
            QueryPerformanceCounter(&t1);
            double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0 / (double)qpcFreq.QuadPart;
            printf("Socket connected (%.2fms)\n", ms);
            fflush(stdout);
            connectTick = GetTickCount64();
            requestDenoiseReset();
            g_streaming.store(true);
            g_micStreaming.store(true);
            if (g_trayIcon) {
                bool dm = g_demandMode.load(std::memory_order_relaxed);
                g_trayIcon->updateIcon(!dm, true);
            }
        }

        uint8_t buffer[BLOCK_SIZE];
        int idleCount = 0;
        int staleCount = 0;
        bool wasIdle = true;

        while (g_running.load()) {
            if (!socketClient.isConnected()) break;

            if (!socketClient.waitForData(100)) {
                staleCount++;
                if (staleCount > 90) {
                    printf("Socket stalled (9s), reconnecting\n");
                    fflush(stdout);
                    socketClient.disconnect();
                    requestDenoiseReset();
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
                requestDenoiseReset();
                g_streaming.store(false);
                g_micStreaming.store(false);
                if (g_trayIcon) g_trayIcon->updateIcon(false, false);
                break;
            }

            bool micRequested = g_micRequested.load(std::memory_order_relaxed);
            bool demandOff = !g_demandMode.load(std::memory_order_relaxed);
            bool renderStalled = g_wasapiOutput && g_wasapiOutput->renderStallScore.load(std::memory_order_relaxed) >= 3;
            bool effectiveActive = demandOff || (micRequested && !renderStalled);

            if (!effectiveActive) {
                if (!wasIdle) requestDenoiseReset();
                g_micStreaming.store(false);
                g_bridgeStatus.store(BRIDGE_IDLE_HOT, std::memory_order_relaxed);
                if (g_trayIcon && !wasIdle) g_trayIcon->updateIcon(false, true);
                idleCount++;
                wasIdle = true;
                if (idleCount % 50 == 0) {
                    g_wasapiOutput->getRingBuffer()->reset();
                }
                if (!g_alwaysHot.load(std::memory_order_relaxed) && idleCount > 500) {
                    printf("Idle 5s, disconnecting socket\n");
                    fflush(stdout);
                    socketClient.disconnect();
                    g_streaming.store(false);
                    g_micStreaming.store(false);
                    if (g_trayIcon) g_trayIcon->updateIcon(false, false);
                    break;
                }
                continue;
            }

            idleCount = 0;
            g_micStreaming.store(true);
            g_bridgeStatus.store(BRIDGE_STREAMING, std::memory_order_relaxed);
            if (g_trayIcon && wasIdle) g_trayIcon->updateIcon(true, true);

            if (wasIdle) {
                requestDenoiseReset();
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
        requestDenoiseReset();
        g_streaming.store(false);
        g_micStreaming.store(false);
        if (g_trayIcon) {
            g_trayIcon->updateIcon(false, false);
        }
        printf("[Bridge] inner loop exited, will retry outer loop\n");
        fflush(stdout);

        if (connectTick > 0) {
            uint64_t elapsed = GetTickCount64() - connectTick;
            if (elapsed < 3000) {
                quickDisconnectCount++;
                printf("Quick disconnect (%llums), count=%d\n",
                       (unsigned long long)elapsed, quickDisconnectCount);
            } else {
                quickDisconnectCount = 0;
            }
            connectTick = 0;
        }

        if (quickDisconnectCount >= 3) {
            printf("3 consecutive quick disconnects, restarting Android app\n");
            fflush(stdout);
            if (!adb.isDeviceOnline(serial)) {
                enterAdbLost("quick disconnects and ADB device is offline");
            } else {
                recoverAdb("3 consecutive quick disconnects");
            }
            quickDisconnectCount = 0;
        }
    }

    socketClient.disconnect();
    adb.cleanup(port);
}

int main(int argc, char* argv[]) {
    // Single instance check (use exe filename as mutex name)
    char exePath[MAX_PATH];
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string exeName = exePath;
    size_t lastSlash = exeName.find_last_of("\\/");
    if (lastSlash != std::string::npos) exeName = exeName.substr(lastSlash + 1);
    std::string mutexName = "Global\\" + exeName;
    HANDLE hMutex = CreateMutexA(NULL, TRUE, mutexName.c_str());
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxA(NULL, (exeName + " is already running.").c_str(), exeName.c_str(), MB_ICONINFORMATION | MB_OK);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    runtime_paths::MigrateLegacyConfigIfNeeded();
    g_config = Config::load();
    syncDspAtomsFromConfig();
    g_demandMode.store(g_config.demandMode, std::memory_order_relaxed);
    g_alwaysHot.store(g_config.alwaysHot, std::memory_order_relaxed);

    bool listDevices = false;
    bool showHelp = false;
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
            showHelp = true;
        }
    }

    if (showHelp || listDevices || g_config.debugConsole) {
        setConsoleVisible(true);
    }

    if (showHelp) {
        printf("Usage: %s [options]\n", argv[0]);
        printf("Options:\n");
        printf("  --list-devices    List audio devices and exit\n");
        printf("  --host <host>     Socket host (default: %s)\n", DEFAULT_HOST);
        printf("  --port <port>     Socket port (default: %d)\n", DEFAULT_PORT);
        printf("  --serial <serial> ADB device serial\n");
        printf("  --help            Show this help\n");
        return 0;
    }

    printf("VoxMic (Raw WASAPI) - Android microphone to Windows\n");
    printf("=============================================================\n\n");
    fflush(stdout);

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

    HWND hWnd = createSettingsWindow(hInstance, &g_config);
    if (!hWnd) {
        printf("ERROR: Failed to create settings window\n");
        return 1;
    }

    TrayIcon trayIcon;
    g_trayIcon = &trayIcon;
    trayIcon.create(hInstance, hWnd);
    trayIcon.setDemandMode(g_config.demandMode);
    trayIcon.setAlwaysHot(g_config.alwaysHot);

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

    g_running.store(false);
    if (bridge.joinable()) bridge.join();
    if (g_monitorThread.joinable()) g_monitorThread.join();
    g_micMonitor.shutdown();
    wasapiOutput.stop();

    KillTimer(hWnd, 1);

    printf("Done.\n");
    fflush(stdout);

    ReleaseMutex(hMutex);
    CloseHandle(hMutex);
    return 0;
}
