#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

// GUI/state-side curve model only. The audio thread never touches this class.
// Runtime DSP uses KickDuck1AudioProcessor's fixed double-buffer instead.
class DuckingCurve
{
public:
    struct Point
    {
        float x = 0.0f;
        float y = 1.0f;
    };

    DuckingCurve()
        : points_{{0.0f, 1.0f}, {1.0f, 1.0f}}
    {
    }

    std::vector<Point> getPoints() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return points_;
    }

    void setPoints(const std::vector<Point>& points)
    {
        std::vector<Point> safe;
        safe.reserve(std::min<std::size_t>(points.size(), 32));
        for (const auto& p : points)
        {
            if (std::isfinite(p.x) && std::isfinite(p.y))
                safe.push_back({ std::clamp(p.x, 0.0f, 1.0f),
                                 std::clamp(p.y, 0.0f, 1.0f) });
        }
        std::sort(safe.begin(), safe.end(),
                  [](const Point& a, const Point& b) { return a.x < b.x; });
        if (safe.empty())
            safe = {{0.0f, 1.0f}, {1.0f, 1.0f}};
        if (safe.front().x > 0.0f)
            safe.insert(safe.begin(), {0.0f, safe.front().y});
        if (safe.back().x < 1.0f)
            safe.push_back({1.0f, safe.back().y});
        if (safe.size() > 32)
            safe.resize(32);

        std::lock_guard<std::mutex> lock(mutex_);
        points_.swap(safe);
    }

    float valueAt(float x) const noexcept
    {
        // This function is intentionally not used by the audio thread.
        std::lock_guard<std::mutex> lock(mutex_);
        if (points_.empty()) return 1.0f;
        if (x <= points_.front().x) return points_.front().y;
        if (x >= points_.back().x) return points_.back().y;
        for (std::size_t i = 1; i < points_.size(); ++i)
        {
            const auto& a = points_[i - 1];
            const auto& b = points_[i];
            if (x <= b.x)
            {
                const float t = (x - a.x) / std::max(1.0e-6f, b.x - a.x);
                return a.y + (b.y - a.y) * t;
            }
        }
        return points_.back().y;
    }

private:
    mutable std::mutex mutex_;
    std::vector<Point> points_;
};
