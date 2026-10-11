#pragma once

#include <algorithm>
#include <cmath>

// Sample-clock metering: no GUI dependence, storage allocation or block-size bias.
class MeterBallistics
{
public:
    void prepare(double sampleRate) noexcept
    {
        const double rate = std::max(1.0, sampleRate);
        rmsDecay = std::exp(-1.0 / (rate * 0.3));
        peakDecay = std::exp(-std::log(10.0) * 24.0 / (20.0 * rate));
        holdSamples = std::max(1, static_cast<int>(rate * 0.1));
        hold = 0;
        peak = power = 0.0;
    }

    void push(double samplePeak, double samplePower) noexcept
    {
        power += (1.0 - rmsDecay) * (samplePower - power);
        if (samplePeak >= peak)
        {
            peak = samplePeak;
            hold = holdSamples;
        }
        else if (hold > 0)
            --hold;
        else
            peak = std::max(samplePeak, peak * peakDecay);
    }

    float getPeak() const noexcept { return static_cast<float>(peak); }
    float getRms() const noexcept { return static_cast<float>(std::sqrt(std::max(0.0, power))); }

private:
    double rmsDecay = 0.0, peakDecay = 0.0;
    double peak = 0.0, power = 0.0;
    int holdSamples = 1, hold = 0;
};
