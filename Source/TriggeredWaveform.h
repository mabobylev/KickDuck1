#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// Audio owns the capture slot; GUI reserves a published slot while copying.
// Three fixed buffers allow capture, publication and reading without waiting.
class TriggeredWaveform
{
public:
    static constexpr int points = 1600;
    static_assert(std::atomic<int>::is_always_lock_free, "Waveform ownership must be lock-free");

    void clear() noexcept
    {
        if (capture >= 0)
            slots[capture].owner.store(0, std::memory_order_release);
        capture = -1;
        begin(1, 0.0);
        finish();
    }

    void trigger(int samples, double seconds) noexcept
    {
        // Finish the previous hit before admitting the new hit. Its remaining
        // bins stay zero, retaining the same beat-wide horizontal time scale.
        finish();
        begin(std::max(1, samples), seconds);
    }

    void push(float input, float output, float kick, int samples = 1, double seconds = 0.0) noexcept
    {
        // Between kicks, keep publishing whole BPM-sized bass frames.
        if (capture < 0)
            begin(std::max(1, samples), seconds);
        if (capture < 0)
            return;
        auto& frame = slots[capture];
        const int bin = static_cast<int>(static_cast<std::int64_t>(position) * points / length);
        // Keep Bass Input/Output from the same sample so reduction stays visible.
        if (std::abs(input) >= std::abs(frame.input[bin]))
        {
            frame.input[bin] = input;
            frame.output[bin] = output;
        }
        if (std::abs(kick) >= std::abs(frame.kick[bin]))
            frame.kick[bin] = kick;
        if (++position >= length)
            finish();
    }

    // False means no new complete frame (or publication in progress). The
    // caller keeps its previous arrays. Never exposes a partially copied frame.
    bool copy(float* input, float* output, float* kick, std::uint64_t& version,
              double& seconds) const noexcept
    {
        if (input == nullptr || output == nullptr || kick == nullptr)
            return false;
        const int index = published.load(std::memory_order_acquire);
        if (index < 0)
            return false;
        auto& frame = slots[index];
        int free = 0;
        if (!frame.owner.compare_exchange_strong(free, 1, std::memory_order_acquire))
            return false;
        const bool changed = frame.version > version;
        if (changed)
        {
            std::copy(frame.input.begin(), frame.input.end(), input);
            std::copy(frame.output.begin(), frame.output.end(), output);
            std::copy(frame.kick.begin(), frame.kick.end(), kick);
            version = frame.version;
            seconds = frame.seconds;
        }
        frame.owner.store(0, std::memory_order_release);
        return changed;
    }

private:
    struct Frame
    {
        std::array<float, points> input {}, output {}, kick {};
        std::uint64_t version = 0;
        double seconds = 0.0;
        // 0 = free, 1 = GUI reading, 2 = audio capturing.
        mutable std::atomic<int> owner {0};
    };
    mutable std::array<Frame, 3> slots;
    std::atomic<int> published {-1};
    int capture = -1, position = 0, length = 1;
    std::uint64_t nextVersion = 0;

    void begin(int samples, double seconds) noexcept
    {
        const int active = published.load(std::memory_order_acquire);
        for (int i = 0; i < static_cast<int>(slots.size()); ++i)
        {
            if (i == active)
                continue;
            int free = 0;
            if (!slots[i].owner.compare_exchange_strong(free, 2, std::memory_order_acquire))
                continue;
            capture = i;
            position = 0;
            length = samples;
            auto& frame = slots[i];
            frame.input.fill(0.0f);
            frame.output.fill(0.0f);
            frame.kick.fill(0.0f);
            frame.seconds = seconds;
            return;
        }
    }

    void finish() noexcept
    {
        if (capture < 0)
            return;
        slots[capture].version = ++nextVersion;
        published.store(capture, std::memory_order_release);
        slots[capture].owner.store(0, std::memory_order_release);
        capture = -1;
    }
};
