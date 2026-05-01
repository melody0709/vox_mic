#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <atomic>
#include <thread>
#include "ring_buffer.h"

#define SAMPLE_RATE 48000
#define INPUT_CHANNELS 1
#define FRAMES_PER_BLOCK 480
#define BYTES_PER_SAMPLE 2
#define BLOCK_SIZE (FRAMES_PER_BLOCK * INPUT_CHANNELS * BYTES_PER_SAMPLE)
#define RING_BUFFER_BLOCKS 128

class WASAPIOutput {
public:
    WASAPIOutput();
    ~WASAPIOutput();

    bool init(bool listDevicesOnly = false);
    void start();
    void stop();

    SPSCRingBuffer* getRingBuffer() { return &m_ringBuffer; }

    std::atomic<int> underruns{0};
    std::atomic<int> receivedBlocks{0};
    std::atomic<int> droppedBlocks{0};

private:
    bool initCOM();
    bool initDeviceEnumerator();
    bool findAndInitDevice();
    bool initAudioClient(IMMDevice* pDevice);
    bool initRenderClient();
    void renderThread();

    IMMDeviceEnumerator* m_pEnumerator{nullptr};
    IMMDevice* m_pDevice{nullptr};
    IAudioClient* m_pAudioClient{nullptr};
    IAudioRenderClient* m_pRenderClient{nullptr};
    WAVEFORMATEX* m_pWaveFormat{nullptr};
    UINT32 m_bufferFrameCount{0};

    UINT32 m_deviceSampleRate{48000};
    UINT32 m_deviceChannels{2};
    UINT32 m_deviceBits{32};
    double m_resampleRatio{1.0};

    SPSCRingBuffer m_ringBuffer{BLOCK_SIZE * RING_BUFFER_BLOCKS};
    std::thread m_renderThread;
    std::atomic<bool> m_running{false};
    HANDLE m_hEvent{nullptr};
};
