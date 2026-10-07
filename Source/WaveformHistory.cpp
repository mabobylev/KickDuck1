#include "WaveformHistory.h"

#include <algorithm>
#include <cmath>

WaveformHistory::WaveformHistory(
    int capacitySamples)
{
    /*
        Always create a valid buffer.

        88200 samples ~= 2 seconds at 44.1 kHz.
    */
    capacitySamples =
        std::max(
            1,
            capacitySamples);

    capacity =
        capacitySamples;

    bassInBuffer =
        std::make_unique<
            std::atomic<float>[]>(
                static_cast<std::size_t>(
                    capacity));

    bassOutBuffer =
        std::make_unique<
            std::atomic<float>[]>(
                static_cast<std::size_t>(
                    capacity));

    kickBuffer =
        std::make_unique<
            std::atomic<float>[]>(
                static_cast<std::size_t>(
                    capacity));

    clear();
}


void WaveformHistory::prepare(
    int capacitySamples)
{
    /*
        IMPORTANT:

        Do NOT reallocate the buffers here.

        The processor may call prepareToPlay() while the GUI timer
        is already running. Replacing the unique_ptr buffers here
        could make copyLatest() access freed memory.

        The history buffer has a fixed capacity.
    */

    if (capacitySamples <= 0)
        return;

    /*
        If the requested capacity is larger than our current
        capacity, we deliberately keep the existing buffer.

        The GUI and DSP code are already bounded to the history
        capacity.
    */

    clear();
}


void WaveformHistory::clear()
{
    writeIndex.store(
        0,
        std::memory_order_release);

    if (capacity <= 0 ||
        bassInBuffer == nullptr ||
        bassOutBuffer == nullptr ||
        kickBuffer == nullptr)
    {
        return;
    }

    for (int i = 0;
         i < capacity;
         ++i)
    {
        bassInBuffer[
            static_cast<std::size_t>(i)]
            .store(
                0.0f,
                std::memory_order_relaxed);

        bassOutBuffer[
            static_cast<std::size_t>(i)]
            .store(
                0.0f,
                std::memory_order_relaxed);

        kickBuffer[
            static_cast<std::size_t>(i)]
            .store(
                0.0f,
                std::memory_order_relaxed);
    }
}


void WaveformHistory::push(
    float bassIn,
    float bassOut,
    float kick)
{
    if (capacity <= 0 ||
        bassInBuffer == nullptr ||
        bassOutBuffer == nullptr ||
        kickBuffer == nullptr)
    {
        return;
    }

    /*
        Keep NaN/Inf out of the visualization buffer.
    */
    if (!std::isfinite(bassIn))
        bassIn = 0.0f;

    if (!std::isfinite(bassOut))
        bassOut = 0.0f;

    if (!std::isfinite(kick))
        kick = 0.0f;

    const std::uint64_t index =
        writeIndex.fetch_add(
            1,
            std::memory_order_relaxed);

    const int position =
        static_cast<int>(
            index %
            static_cast<std::uint64_t>(
                capacity));

    bassInBuffer[
        static_cast<std::size_t>(position)]
        .store(
            bassIn,
            std::memory_order_relaxed);

    bassOutBuffer[
        static_cast<std::size_t>(position)]
        .store(
            bassOut,
            std::memory_order_relaxed);

    kickBuffer[
        static_cast<std::size_t>(position)]
        .store(
            kick,
            std::memory_order_relaxed);
}


void WaveformHistory::copyLatest(
    std::vector<float>& bassIn,
    std::vector<float>& bassOut,
    std::vector<float>& kick,
    int samplesToCopy) const
{
    /*
        Validate everything before touching the arrays.
    */
    if (capacity <= 0 ||
        bassInBuffer == nullptr ||
        bassOutBuffer == nullptr ||
        kickBuffer == nullptr)
    {
        bassIn.clear();
        bassOut.clear();
        kick.clear();
        return;
    }

    /*
        Absolute hard limit.

        Nothing outside the allocated ring buffer can ever be read.
    */
    samplesToCopy =
        std::clamp(
            samplesToCopy,
            0,
            capacity);

    if (samplesToCopy <= 0)
    {
        bassIn.clear();
        bassOut.clear();
        kick.clear();
        return;
    }

    /*
        Read the write position once.

        The audio thread can continue writing while we copy.
        Each individual sample is atomic, so this is safe for the
        visualization. At worst one sample at the boundary belongs
        to a newer/older snapshot.
    */
    const std::uint64_t currentWriteIndex =
        writeIndex.load(
            std::memory_order_acquire);

    const std::uint64_t available =
        std::min(
            currentWriteIndex,
            static_cast<std::uint64_t>(
                capacity));

    const int actualSamples =
        std::min(
            samplesToCopy,
            static_cast<int>(
                available));

    if (actualSamples <= 0)
    {
        bassIn.clear();
        bassOut.clear();
        kick.clear();
        return;
    }

    /*
        Resize destination vectors only to the amount that can
        actually be read.
    */
    bassIn.resize(
        static_cast<std::size_t>(
            actualSamples));

    bassOut.resize(
        static_cast<std::size_t>(
            actualSamples));

    kick.resize(
        static_cast<std::size_t>(
            actualSamples));

    const std::uint64_t firstIndex =
        currentWriteIndex
        -
        static_cast<std::uint64_t>(
            actualSamples);

    for (int i = 0;
         i < actualSamples;
         ++i)
    {
        const std::uint64_t absoluteIndex =
            firstIndex
            +
            static_cast<std::uint64_t>(i);

        const int position =
            static_cast<int>(
                absoluteIndex
                %
                static_cast<std::uint64_t>(
                    capacity));

        const std::size_t p =
            static_cast<std::size_t>(
                position);

        bassIn[
            static_cast<std::size_t>(i)] =
            bassInBuffer[p].load(
                std::memory_order_relaxed);

        bassOut[
            static_cast<std::size_t>(i)] =
            bassOutBuffer[p].load(
                std::memory_order_relaxed);

        kick[
            static_cast<std::size_t>(i)] =
            kickBuffer[p].load(
                std::memory_order_relaxed);
    }
}
