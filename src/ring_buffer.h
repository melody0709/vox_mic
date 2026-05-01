#pragma once

#include <vector>
#include <atomic>
#include <cstring>
#include <cstdint>

class SPSCRingBuffer {
public:
    explicit SPSCRingBuffer(size_t capacity)
        : m_buffer(capacity)
        , m_capacity(capacity)
    {
    }

    bool push(const uint8_t* data, size_t size) {
        size_t writePos = m_writePos.load(std::memory_order_relaxed);
        size_t readPos = m_readPos.load(std::memory_order_acquire);

        if (size > availableWrite(writePos, readPos)) {
            return false;
        }

        if (writePos + size <= m_capacity) {
            memcpy(&m_buffer[writePos], data, size);
        } else {
            size_t first = m_capacity - writePos;
            memcpy(&m_buffer[writePos], data, first);
            memcpy(&m_buffer[0], data + first, size - first);
        }

        m_writePos.store((writePos + size) % m_capacity, std::memory_order_release);
        return true;
    }

    bool pop(uint8_t* data, size_t size) {
        size_t readPos = m_readPos.load(std::memory_order_relaxed);
        size_t writePos = m_writePos.load(std::memory_order_acquire);

        if (size > availableRead(readPos, writePos)) {
            return false;
        }

        if (readPos + size <= m_capacity) {
            memcpy(data, &m_buffer[readPos], size);
        } else {
            size_t first = m_capacity - readPos;
            memcpy(data, &m_buffer[readPos], first);
            memcpy(data + first, &m_buffer[0], size - first);
        }

        m_readPos.store((readPos + size) % m_capacity, std::memory_order_release);
        return true;
    }

    void reset() {
        m_readPos.store(0, std::memory_order_relaxed);
        m_writePos.store(0, std::memory_order_relaxed);
    }

    size_t size() const {
        size_t readPos = m_readPos.load(std::memory_order_relaxed);
        size_t writePos = m_writePos.load(std::memory_order_relaxed);
        return availableRead(readPos, writePos);
    }

    size_t sizeBlocks(size_t blockSize) const {
        return size() / blockSize;
    }

    void discard(size_t bytes) {
        size_t readPos = m_readPos.load(std::memory_order_relaxed);
        size_t writePos = m_writePos.load(std::memory_order_acquire);
        size_t avail = availableRead(readPos, writePos);
        if (bytes >= avail) {
            m_readPos.store(writePos, std::memory_order_release);
        } else {
            m_readPos.store((readPos + bytes) % m_capacity, std::memory_order_release);
        }
    }

private:
    size_t availableRead(size_t readPos, size_t writePos) const {
        if (writePos >= readPos) {
            return writePos - readPos;
        }
        return m_capacity - readPos + writePos;
    }

    size_t availableWrite(size_t writePos, size_t readPos) const {
        return m_capacity - availableRead(readPos, writePos) - 1;
    }

    std::vector<uint8_t> m_buffer;
    size_t m_capacity;
    std::atomic<size_t> m_readPos{0};
    std::atomic<size_t> m_writePos{0};
};
