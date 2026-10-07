#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

class DuckingCurve
{
public:
    struct Point
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    DuckingCurve();

    // Thread-safe for concurrent audio-thread reads and GUI-thread edits.
    float valueAt(float x) const;

    std::vector<Point> getPoints() const;
    void setPoints(const std::vector<Point>& newPoints);
    void setPoint(std::size_t index, float x, float y);
    void addPoint(float x, float y);
    void removePoint(std::size_t index);

    std::size_t size() const noexcept;

private:
    static float clamp01(float value);
    static void sanitise(std::vector<Point>& p);

    // Immutable snapshots. The audio thread only loads a snapshot; the GUI
    // creates a new vector and atomically publishes it. No mutex and no
    // concurrent mutation of the vector used by processBlock().
    std::shared_ptr<const std::vector<Point>> points;
};
