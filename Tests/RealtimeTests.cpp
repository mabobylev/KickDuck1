#include "TriggeredWaveform.h"
#include "DuckingCurve.h"
#include "WaveformHistory.h"
#include <iostream>
#include <memory>
#include <thread>

int main()
{
    auto history = std::make_unique<WaveformHistory>(1024);
    auto frames = std::make_unique<TriggeredWaveform>();
    DuckingCurve curve;
    curve.setPoints({{0, 0.5f}, {1, 0.5f}});
    std::atomic<bool> started {false}, finished {false}, valid {true};
    std::thread reader([&]
    {
        std::array<float, TriggeredWaveform::points> input {}, output {}, kick {}, reduction {};
        std::vector<DuckingCurve::Point> points;
        points.reserve(DuckingCurve::maxPoints);
        std::uint64_t version = 0;
        double seconds = 0.0;
        started.store(true);
        do
        {
            if (curve.tryCopyPoints(points))
                for (const auto& point : points)
                    if (point.y != points.front().y) valid.store(false);
            history->copyLatest(input.data(), output.data(), kick.data(), 64, 1024, false, reduction.data());
            for (int i = 0; i < 64; ++i)
                if (output[i] != input[i] * 2 || kick[i] != -input[i] || reduction[i] != input[i] * 0.25f)
                    valid.store(false);
            if (frames->copy(input.data(), output.data(), kick.data(), version, seconds))
                for (int i = 0; i < TriggeredWaveform::points; ++i)
                    if (output[i] != input[i] * 2 || kick[i] != -input[i]) valid.store(false);
        } while (!finished.load());
    });
    while (!started.load()) std::this_thread::yield();
    for (int i = 0; i < 300000; ++i)
    {
        if (i % 256 == 0) curve.setPoints({{0, 0.25f}, {0.5f, 0.25f}, {1, 0.25f}});
        if (i % 4096 == 0) history->clear();
        if (i % 103 == 0) frames->trigger(1600, 0.5);
        if (i % 8192 == 0) frames->clear();
        const float value = static_cast<float>((i % 31) + 1) / 32.0f;
        history->push(value, value * 2, -value, value * 0.25f);
        frames->push(value, value * 2, -value, 1600, 0.5);
    }
    finished.store(true);
    reader.join();
    if (!valid.load())
    {
        std::cerr << "FAIL: incoherent concurrent waveform snapshot\n";
        return 1;
    }
    std::cout << "PASS: concurrent history overwrite/reset triggered frame ownership and GUI curve model\n";
}
