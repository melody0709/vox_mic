#pragma once

// Epoch-tagged single-producer / single-consumer ring of audio blocks.
//
// push() runs on the WASAPI render thread and pop() on the DPDFNet worker, so
// every hand-off is a lock-free store and nothing here allocates. A block
// carries the stream epoch so output belonging to a previous stream can be
// discarded instead of played.
//
// Extracted from dpdfnet_processor.cpp: it is a self-contained data structure,
// and that file sits on its line baseline.

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>

#include "dsp/dpdfnet_processor.h"  // DPDFNET_BLOCK_SAMPLES

template <size_t Capacity>
class TaggedBlockQueue {
public:
    struct Block {
        uint64_t epoch = 0;
        float samples[DPDFNET_BLOCK_SAMPLES]{};
    };

    bool push(uint64_t epoch, std::span<const float> samples) {
        if (samples.size() < DPDFNET_BLOCK_SAMPLES) return false;
        const size_t write = m_write.load(std::memory_order_relaxed);
        const size_t next = (write + 1) % Capacity;
        if (next == m_read.load(std::memory_order_acquire)) return false;

        m_blocks[write].epoch = epoch;
        std::memcpy(m_blocks[write].samples, samples.data(),
            sizeof(m_blocks[write].samples));
        m_write.store(next, std::memory_order_release);
        return true;
    }

    bool pop(Block& block) {
        const size_t read = m_read.load(std::memory_order_relaxed);
        if (read == m_write.load(std::memory_order_acquire)) return false;

        block = m_blocks[read];
        m_read.store((read + 1) % Capacity, std::memory_order_release);
        return true;
    }

    // Called only by the single consumer side of this queue.
    void discardAll() {
        m_read.store(m_write.load(std::memory_order_acquire),
            std::memory_order_release);
    }

private:
    std::array<Block, Capacity> m_blocks{};
    std::atomic<size_t> m_read{0};
    std::atomic<size_t> m_write{0};
};
