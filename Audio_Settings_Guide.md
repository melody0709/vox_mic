# Microphone Audio Parameter Tuning Guide

[简体中文](doc/zh-CN/Audio_Settings_Guide.md) | **English**

This guide explains each parameter in the audio settings panel in detail, helping you tune the clearest, most natural voice for different use scenarios.

---

## Part 1: Basic Input Settings

This section controls the microphone's raw input and hardware-level processing from the system (e.g., Android底层).

### 1. Gain (Input Gain)
*   **Purpose**: Controls the amplification multiplier of the microphone's raw audio.
*   **How to tune**:
    *   **1.00x (Recommended)**: Keep the microphone's original volume. If your voice is already loud enough, keeping 1.0x maximizes avoidance of clipping (audio distortion).
    *   **> 1.00x**: If you speak softly or the microphone is far from your mouth, increase slightly (e.g., 1.2x - 1.5x). Note that increasing gain also amplifies background noise.

### 2. NoiseSuppressor (System Hardware Noise Suppression)
*   **Purpose**: Invokes the device's (e.g., phone system) built-in noise suppression algorithm.
*   **How to tune**: **Recommended OFF** (uncheck).
    *   **Reason**: If you plan to use the `Noise Reduction` (DSP advanced denoising) below, do NOT enable this. Enabling both causes "double denoising" which over-cuts the audio signal, making your voice sound muffled and robotic. Leave denoising to the smarter DSP module below.

### 3. AcousticEchoCanceler (Acoustic Echo Cancellation - AEC)
*   **Purpose**: Eliminates echo caused by speaker audio being picked up by the microphone.
*   **How to tune**:
    *   **If wearing headphones**: Can be **OFF**, since headphones don't produce echo, and disabling reduces slight audio quality loss.
    *   **If using speakers**: Must be **ON**, otherwise the other party will hear their own echo.

### 4. AutomaticGainControl (System Automatic Gain Control - AGC)
*   **Purpose**: The system's built-in "automatic volume adjuster" -- raises volume when quiet, lowers when loud.
*   **How to tune**: **Recommended OFF** (uncheck).
    *   **Reason**: If you enable `Compressor Enable` (compressor) below, the two will conflict, causing volume to fluctuate unnaturally. Disable system AGC and let the DSP compressor handle dynamic control.

---

## Part 2: DSP Advanced Audio Processing

This is the core section, using software algorithms to finely craft the audio, determining the final "texture" and "clarity" of your voice.

### 1. EQ Enable (Equalizer On)
*   **Purpose**: Master switch -- when enabled, `Presence` and `Bass Cut` below take effect. Strongly recommended **ON**.

#### 1.1 Presence (Presence / High-frequency Compensation)
*   **Purpose**: Boosts the mid-high frequency range (typically around 2kHz - 6kHz). This determines the **"clarity", "penetration", and "sibilance"** of your voice.
*   **How to tune**:
    *   Think of it like adjusting a TV's "sharpness". After enabling noise reduction, voice tends to sound muffled; increasing this value (e.g., **+3.0 dB to +5.0 dB**) can recover the lost clarity.
    *   *Note*: If set too high (e.g., above +8dB), voice becomes harsh and sibilance (hissing) becomes severe.

#### 1.2 Bass Cut (Low-frequency Cut / High-pass Filter)
*   **Purpose**: Attenuates the low frequency range (typically below 100Hz).
*   **How to tune**:
    *   **-3.0 dB to -6.0 dB (Recommended)**: Effectively filters out microphone plosives (popping sounds), desk vibration, and distant environmental rumble, making voice sound cleaner.

### 2. Compressor Enable (Compressor)
*   **Purpose**: The "iron" for audio dynamic range. It pushes down loud sounds and pulls up quiet sounds, keeping overall volume steady.
*   **How to tune**: **Recommended ON**.
    *   This prevents clipping when you shout excitedly and ensures you're heard when mumbling softly. It's smoother and more professional than system AGC.

### 3. Noise Reduction (DSP Software Denoising / e.g., RNNoise)
*   **Purpose**: Uses advanced algorithms (e.g., neural networks) to eliminate background noise like fan sounds, keyboard clicks, etc.
*   **How to tune**: Depends on your environment.
    *   **Noisy environment (Recommended ON)**: Excellent noise reduction, but at the cost of slightly eating into high frequencies (making voice sound muffled). **Must be paired with increased `Presence`**.
    *   **Quiet room (Recommended OFF)**: If your room is already quiet, disabling this gives the most perfect, faithful broadcast-quality voice.

---

## Summary: Recommended Configurations by Scenario

### Scenario A: Daily Use / Room with Some Background Noise (Your Current Config - Recommended)
*   **System processing**: All OFF (only keep AEC on as needed). Gain set to 1.3x.
*   **DSP Denoising**: ON (`Noise Reduction` checked)
*   **EQ Compensation**: ON. `Presence` +4.0dB, `Bass Cut` -3.0dB.
*   **Dynamic Control**: Compressor ON (`Compressor` checked).
*   *Result*: No environmental noise, clear and stable voice, suitable for 90% of scenarios.

### Scenario B: Absolutely Quiet Room / Pursuing Ultimate Fidelity
*   **System processing**: All OFF.
*   **DSP Denoising**: **OFF** (`Noise Reduction` unchecked).
*   **EQ Compensation**: ON. `Presence` +1.0dB (slight brightness boost only), `Bass Cut` -3.0dB (prevent low-frequency rumble).
*   **Dynamic Control**: Compressor ON (`Compressor` checked).
*   *Result*: Maximum preservation of real voice detail, best audio quality, but requires a quiet environment.
