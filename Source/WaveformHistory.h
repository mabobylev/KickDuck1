#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

class WaveformHistory
{
public:
    explicit WaveformHistory(int capacitySamples = 88200);

    ~WaveformHistory() = default;

    void prepare(int capacitySamples);

    void clear();

    void push(
        float bassIn,
        float bassOut,
        float kick);

    void copyLatest(
        std::vector<float>& bassIn,
        std::vector<float>& bassOut,
        std::vector<float>& kick,
        int samplesToCopy) const;

    int getCapacity() const noexcept
    {
        return capacity;
    }

private:
    /*
        IMPORTANT:

        The buffers are allocated once and never replaced during
        normal plugin operation.

        This prevents the GUI timer from reading freed memory while
        the audio thread / host is preparing the processor.
    */
    int capacity = 0;

    std::unique_ptr<std::atomic<float>[]> bassInBuffer;
    std::unique_ptr<std::atomic<float>[]> bassOutBuffer;
    std::unique_ptr<std::atomic<float>[]> kickBuffer;

    std::atomic<std::uint64_t> writeIndex { 0 };

    WaveformHistory(const WaveformHistory&) = delete;
    WaveformHistory& operator=(
        const WaveformHistory&) = delete;
};
