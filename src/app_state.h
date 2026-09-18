#pragma once

#include <atomic>
#include <cstdint>

#include "config.h"

class TrayIcon;

// Which denoiser the pipeline is actually running. Lives here because it is
// part of the shared state vocabulary; pipeline.h re-exports it.
enum class DenoiseBackendKind : int {
    Rnnoise = 0,
    Dpdfnet = 1,
    Off = 2,
};

// Where the DPDFNet session sits in its on-demand lifecycle.
//
// The payload is ~43 MB resident (onnxruntime plus a 10 MB model), measured, so
// a user who selected RNNoise must never load it. The UI needs to tell "not
// loaded yet" apart from "tried and failed", which a single bool cannot
// express - it would report "unavailable" for a backend the user never asked
// to use.
enum class DpdfnetLoadState : int {
    NotLoaded = 0,  // nothing loaded; will load on demand
    Loading = 1,
    Ready = 2,
    Failed = 3,     // the payload is present but refused to start
};

// Single owner of everything shared across threads.
//
// Before this existed the state was ~20 free globals defined in main.cpp and
// re-declared as `extern` in every translation unit that touched them, so the
// UI, the platform layer and the DSP pipeline all reached directly into each
// other's internals. They now live here behind one name.
//
// Members marked "settings" are written from the UI thread and read from the
// WASAPI render thread, which is why they stay lock-free atomics with explicit
// memory ordering.
struct AppState {
    // ---- DSP settings: UI thread writes, render thread reads ----
    std::atomic<float> gain{1.5f};
    std::atomic<bool> eqEnabled{false};
    std::atomic<float> eqPresence{3.0f};
    std::atomic<float> eqBassCut{-3.0f};
    std::atomic<bool> compressorEnabled{false};
    std::atomic<bool> nrEnabled{true};
    std::atomic<float> nrStrength{0.6f};
    std::atomic<int> denoiseBackend{static_cast<int>(DenoiseBackendKind::Dpdfnet)};
    std::atomic<uint64_t> denoiseResetEpoch{1};
    std::atomic<bool> dpdfnetAvailable{false};
    std::atomic<bool> dpdfnetDegraded{false};
    // DpdfnetLoadState. dpdfnetAvailable above stays the "is it usable" bool so
    // existing degrade logic is untouched; this one adds the not-loaded case.
    std::atomic<int> dpdfnetLoadState{
        static_cast<int>(DpdfnetLoadState::NotLoaded)};
    std::atomic<int> denoiseEffectiveBackend{
        static_cast<int>(DenoiseBackendKind::Rnnoise)};

    // ---- runtime flags ----
    std::atomic<bool> running{true};
    std::atomic<bool> micRequested{false};
    std::atomic<bool> demandMode{true};
    std::atomic<bool> alwaysHot{true};
    std::atomic<uint64_t> micOnTick{0};

    // ---- non-realtime state: only touched off the render thread ----
    Config config;
    TrayIcon* trayIcon{nullptr};
};

// A plain namespace-scope instance rather than a lazy function-local singleton:
// the render thread reads these atomics on every block, and a magic static
// would add a guard-variable check to each access. `inline` keeps a single
// definition across translation units without any `extern` declaration.
inline AppState g_appState{};

// ---- commands the UI issues against the engine -------------------------------
// Declared here so callers no longer re-declare them as `extern`. The `extern`
// keyword was redundant on function declarations anyway.
void syncDspAtomsFromConfig(const Config& cfg);
void syncDspAtomsFromConfig();
void requestDenoiseReset();
void setDemandModeRuntime(bool enabled);
