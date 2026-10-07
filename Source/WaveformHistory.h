#pragma once

#include <array>
#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cmath>

// Lock-free, fixed-capacity DSP history.
// Audio thread writes samples; GUI only reads them through copyLatest().
// No shared std::vector and no allocation after construction.
class WaveformHistory
{
public:
    explicit WaveformHistory(int capacity)
        : capacity_(std::clamp(capacity, 1024, maxCapacity))
    {
        clear();
    }

    void clear() noexcept
    {
        writeIndex_.store(0, std::memory_order_relaxed);
        for (int i = 0; i < capacity_; ++i)
            sequence_[i].store(0, std::memory_order_relaxed);
    }

    void push(float bassIn, float bassOut, float kick) noexcept
    {
        const std::uint32_t index =
            writeIndex_.fetch_add(1, std::memory_order_relaxed) % static_cast<std::uint32_t>(capacity_);

        auto& seq = sequence_[index];
        const std::uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s | 1u, std::memory_order_release); // writer owns slot
        bassIn_[index].store(bassIn, std::memory_order_relaxed);
        bassOut_[index].store(bassOut, std::memory_order_relaxed);
        kick_[index].store(kick, std::memory_order_relaxed);
        seq.store((s | 1u) + 1u, std::memory_order_release); // stable/even
    }

    // Downsample the latest history directly into caller-owned fixed arrays.
    // points is normally 1600. No allocation and no locks.
    void copyLatest(float* bassInOut,
                    float* bassOutOut,
                    float* kickOut,
                    int points,
                    int samplesToCopy) const noexcept
    {
        if (bassInOut == nullptr || bassOutOut == nullptr || kickOut == nullptr || points <= 0)
            return;

        const int available = std::min(samplesToCopy, capacity_);
        const std::uint32_t end = writeIndex_.load(std::memory_order_acquire);
        const std::uint32_t first = end >= static_cast<std::uint32_t>(available)
            ? end - static_cast<std::uint32_t>(available) : 0u;

        for (int p = 0; p < points; ++p)
        {
            const int beginOffset = static_cast<int>((static_cast<std::int64_t>(p) * available) / points);
            const int endOffset = std::max(beginOffset + 1,
                static_cast<int>((static_cast<std::int64_t>(p + 1) * available) / points));

            float in = 0.0f, out = 0.0f, kick = 0.0f;
            float strongest = -1.0f;

            for (int o = beginOffset; o < std::min(endOffset, available); ++o)
            {
                const std::uint32_t index =
                    (first + static_cast<std::uint32_t>(o)) % static_cast<std::uint32_t>(capacity_);

                // Sequence check gives a coherent triple for each slot.
                for (int retry = 0; retry < 3; ++retry)
                {
                    const std::uint32_t a = sequence_[index].load(std::memory_order_acquire);
                    if (a & 1u) continue;
                    const float vi = bassIn_[index].load(std::memory_order_relaxed);
                    const float vo = bassOut_[index].load(std::memory_order_relaxed);
                    const float vk = kick_[index].load(std::memory_order_relaxed);
                    const std::uint32_t b = sequence_[index].load(std::memory_order_acquire);
                    if (a == b)
                    {
                        const float magnitude = std::max({ std::abs(vi), std::abs(vo), std::abs(vk) });
                        if (magnitude > strongest)
                        {
                            strongest = magnitude;
                            in = vi;
                            out = vo;
                            kick = vk;
                        }
                        break;
                    }
                }
            }

            bassInOut[p] = in;
            bassOutOut[p] = out;
            kickOut[p] = kick;
        }
    }

    int capacity() const noexcept { return capacity_; }

private:
    static constexpr int maxCapacity = 131072;
    const int capacity_;

    alignas(64) std::atomic<std::uint32_t> writeIndex_ { 0 };
    std::array<std::atomic<std::uint32_t>, maxCapacity> sequence_ {};
    std::array<std::atomic<float>, maxCapacity> bassIn_ {};
    std::array<std::atomic<float>, maxCapacity> bassOut_ {};
    std::array<std::atomic<float>, maxCapacity> kick_ {};
};
