#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <vector>

namespace peq
{

/** Lock-free single-producer / single-consumer ring buffer of floats.

    The audio thread pushes, the GUI thread pops. If the GUI falls behind,
    new samples that do not fit are dropped (the analyser simply skips a
    little audio); the audio thread never waits or allocates.
*/
class SampleFifo
{
public:
    /** Not real-time safe; call before audio starts. Capacity is rounded up
        to a power of two. */
    void setCapacity (size_t minCapacity)
    {
        size_t c = 1;
        while (c < minCapacity + 1)
            c <<= 1;
        buffer.assign (c, 0.0f);
        mask = c - 1;
        readPos.store (0);
        writePos.store (0);
    }

    size_t capacity() const noexcept { return buffer.empty() ? 0 : buffer.size() - 1; }

    /** Producer side. Returns how many samples were written. */
    size_t push (const float* data, size_t count) noexcept
    {
        if (buffer.empty())
            return 0;
        const size_t w = writePos.load (std::memory_order_relaxed);
        const size_t r = readPos.load (std::memory_order_acquire);
        const size_t freeSpace = mask - ((w - r) & mask);
        const size_t n = std::min (count, freeSpace);
        for (size_t i = 0; i < n; ++i)
            buffer[(w + i) & mask] = data[i];
        writePos.store ((w + n) & mask, std::memory_order_release);
        return n;
    }

    /** Consumer side. Returns how many samples were read. */
    size_t pop (float* dest, size_t maxCount) noexcept
    {
        if (buffer.empty())
            return 0;
        const size_t r = readPos.load (std::memory_order_relaxed);
        const size_t w = writePos.load (std::memory_order_acquire);
        const size_t available = (w - r) & mask;
        const size_t n = std::min (maxCount, available);
        for (size_t i = 0; i < n; ++i)
            dest[i] = buffer[(r + i) & mask];
        readPos.store ((r + n) & mask, std::memory_order_release);
        return n;
    }

    size_t available() const noexcept
    {
        return (writePos.load (std::memory_order_acquire) - readPos.load (std::memory_order_acquire)) & mask;
    }

private:
    std::vector<float> buffer;
    size_t mask = 0;
    std::atomic<size_t> readPos { 0 }, writePos { 0 };
};

} // namespace peq
