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
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free, "History publication must be lock-free");

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

    void push(float bassIn, float bassOut, float kick, float reduction = 0.0f) noexcept
    {
        const std::uint64_t end = writeIndex_.load(std::memory_order_relaxed);
        const auto index = static_cast<std::size_t>(end % static_cast<std::uint64_t>(capacity_));

        auto& seq = sequence_[index];
        const std::uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s | 1u, std::memory_order_release); // writer owns slot
        bassIn_[index].store(bassIn, std::memory_order_relaxed);
        bassOut_[index].store(bassOut, std::memory_order_relaxed);
        kick_[index].store(kick, std::memory_order_relaxed);
        reduction_[index].store(reduction, std::memory_order_relaxed);
        seq.store((s | 1u) + 1u, std::memory_order_release); // stable/even
        // Publish only after the newest sample triple has been written.
        writeIndex_.store(end + 1u, std::memory_order_release);
    }

    // Downsample the latest history directly into caller-owned fixed arrays.
    // points is normally 1600. No allocation and no locks.
    void copyLatest(float* bassInOut,
                    float* bassOutOut,
                    float* kickOut,
                    int points,
                    int samplesToCopy, bool includeKick = true, float* reductionOut = nullptr) const noexcept
    {
        if (bassInOut == nullptr || bassOutOut == nullptr || kickOut == nullptr || points <= 0)
            return;

        const int available = std::clamp(samplesToCopy, 1, capacity_);
        const std::uint64_t end = writeIndex_.load(std::memory_order_acquire);
        const auto present = std::min(end, static_cast<std::uint64_t>(available));
        const auto first = end - present;
        const int padding = available - static_cast<int>(present);

        for (int p = 0; p < points; ++p)
        {
            const int beginOffset = static_cast<int>((static_cast<std::int64_t>(p) * available) / points);
            const int endOffset = std::max(beginOffset + 1,
                static_cast<int>((static_cast<std::int64_t>(p + 1) * available) / points));

            float in = 0.0f, out = 0.0f, kick = 0.0f;
            float strongest = -1.0f, maxReduction = 0.0f;

            for (int o = beginOffset; o < std::min(endOffset, available); ++o)
            {
                // Missing startup history stays on the left; newest audio is right.
                if (o < padding) continue;
                const auto index = static_cast<std::size_t>((first + static_cast<std::uint64_t>(o - padding))
                    % static_cast<std::uint64_t>(capacity_));

                const auto absolute = first + static_cast<std::uint64_t>(o - padding);
                const auto expectedSequence = static_cast<std::uint32_t>(2u * (absolute / capacity_ + 1u));
                // Reject slots that were overwritten after the snapshot began.
                // Sequence check gives a coherent triple for each slot.
                for (int retry = 0; retry < 3; ++retry)
                {
                    const std::uint32_t a = sequence_[index].load(std::memory_order_acquire);
                    if (a == 0u || a != expectedSequence || (a & 1u)) continue;
                    const float vi = bassIn_[index].load(std::memory_order_relaxed);
                    const float vo = bassOut_[index].load(std::memory_order_relaxed);
                    const float vk = kick_[index].load(std::memory_order_relaxed);
                    const float vr = reduction_[index].load(std::memory_order_relaxed);
                    const std::uint32_t b = sequence_[index].load(std::memory_order_acquire);
                    if (a == b)
                    {
                        maxReduction = std::max(maxReduction, vr);
                        const float magnitude = includeKick ? std::max({ std::abs(vi), std::abs(vo), std::abs(vk) })
                            : std::max(std::abs(vi), std::abs(vo));
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
            if (reductionOut != nullptr) reductionOut[p] = maxReduction;
        }
    }

    int capacity() const noexcept { return capacity_; }

private:
    static constexpr int maxCapacity = 524288;
    const int capacity_;

    alignas(64) std::atomic<std::uint64_t> writeIndex_ { 0 };
    std::array<std::atomic<std::uint32_t>, maxCapacity> sequence_ {};
    std::array<std::atomic<float>, maxCapacity> bassIn_ {};
    std::array<std::atomic<float>, maxCapacity> bassOut_ {};
    std::array<std::atomic<float>, maxCapacity> kick_ {};
    std::array<std::atomic<float>, maxCapacity> reduction_ {};
};
