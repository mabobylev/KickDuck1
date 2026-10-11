#pragma once

#include <algorithm>
#include <cmath>
#include <mutex>
#include <vector>

// GUI/state-side curve model only. The audio thread never touches this class.
// Runtime DSP uses KickDuck1AudioProcessor's fixed runtime buffers instead.
class DuckingCurve
{
public:
    struct Point
    {
        float x = 0.0f;
        float y = 1.0f;
    };

    static constexpr int maxPoints = 32;

    static std::vector<Point> defaultPoints()
    {
        // Y is reduction: bottom = full duck, top = recovered bass.
        return {{0.0f, 1.0f}, {0.25f, 0.85f}, {0.5f, 0.0f},
                {0.9f, 0.1f}, {1.0f, 1.0f}};
    }

    enum class Preset { classic, punch, smooth, longDuck, swell, gate };
    static constexpr int presetCount = 6;

    static const char* presetName(Preset preset) noexcept
    {
        switch (preset)
        {
            case Preset::classic: return "Classic";
            case Preset::punch: return "Punch";
            case Preset::smooth: return "Smooth";
            case Preset::longDuck: return "Long";
            case Preset::swell: return "Swell";
            case Preset::gate: return "Gate";
        }
        return "Classic";
    }

    static std::vector<Point> presetPoints(Preset preset)
    {
        switch (preset)
        {
            case Preset::classic: return defaultPoints();
            case Preset::punch: return {{0, 1}, {0.075f, 0.95f}, {0.22f, 0}, {0.9f, 0}, {1, 0}};
            case Preset::smooth: return {{0, 1}, {0.15f, 0.98f}, {0.65f, 0}, {0.92f, 0}, {1, 0}};
            case Preset::longDuck: return {{0, 1}, {0.32f, 1}, {0.78f, 0}, {0.94f, 0}, {1, 0}};
            case Preset::swell: return {{0, 1}, {0.12f, 0.7f}, {0.42f, 0.05f}, {0.88f, 0.05f}, {1, 1}};
            case Preset::gate: return {{0, 1}, {0.22f, 1}, {0.25f, 0}, {0.94f, 0}, {1, 0}};
        }
        return defaultPoints();
    }

    // Shared bounded interpolation for GUI, preset thumbnails and DSP.
    // Harmonic tangents preserve monotonic segments. Extrema and endpoints have
    // zero tangent; the curve passes through every handle without overshoot.
    static float sample(const Point* points, int count, float x, bool rounded) noexcept
    {
        if (count <= 0) return 0.0f;
        if (x < points[0].x && points[0].x > 0.0f)
        {
            const float t = std::clamp(x / points[0].x, 0.0f, 1.0f);
            return points[0].y * (rounded ? t * t * (3.0f - 2.0f * t) : t);
        }
        if (count == 1 || x <= points[0].x) return points[0].y;
        if (x >= points[count - 1].x) return points[count - 1].y;
        const auto tangent = [points, count](int index)
        {
            if (index <= 0 || index >= count - 1) return 0.0f;
            const float left = (points[index].y - points[index - 1].y)
                / std::max(1.0e-6f, points[index].x - points[index - 1].x);
            const float right = (points[index + 1].y - points[index].y)
                / std::max(1.0e-6f, points[index + 1].x - points[index].x);
            if (left == 0.0f || right == 0.0f || (left < 0.0f) != (right < 0.0f)) return 0.0f;
            return 2.0f * left * right / (left + right);
        };
        for (int i = 1; i < count; ++i)
        {
            const auto& a = points[i - 1];
            const auto& b = points[i];
            if (x > b.x) continue;
            const float span = std::max(1.0e-6f, b.x - a.x);
            const float t = std::clamp((x - a.x) / span, 0.0f, 1.0f);
            if (!rounded) return a.y + (b.y - a.y) * t;
            const float m0 = tangent(i - 1) * span;
            const float m1 = tangent(i) * span;
            const float delta = b.y - a.y;
            const float value = a.y + t * (m0 + t * (3.0f * delta - 2.0f * m0 - m1
                                                   + t * (m0 + m1 - 2.0f * delta)));
            return std::clamp(value, std::min(a.y, b.y), std::max(a.y, b.y));
        }
        return points[count - 1].y;
    }

    DuckingCurve()
        : points_(defaultPoints())
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
        safe.reserve(maxPoints);
        for (const auto& p : points)
        {
            if (std::isfinite(p.x) && std::isfinite(p.y))
                safe.push_back({ std::clamp(p.x, 0.0f, 1.0f),
                                 std::clamp(p.y, 0.0f, 1.0f) });
            if (safe.size() == maxPoints)
                break;
        }
        std::stable_sort(safe.begin(), safe.end(),
                         [](const Point& a, const Point& b) { return a.x < b.x; });
        // At identical X positions the last supplied point wins.
        std::size_t count = 0;
        for (const auto p : safe)
        {
            if (count > 0 && safe[count - 1].x == p.x)
                safe[count - 1] = p;
            else
                safe[count++] = p;
        }
        safe.resize(count);
        if (safe.empty())
            safe = defaultPoints();
        if (safe.front().x > 0.0f)
        {
            if (safe.size() == maxPoints)
                safe.erase(safe.begin() + 1);
            safe.insert(safe.begin(), {0.0f, safe.front().y});
        }
        if (safe.back().x < 1.0f)
        {
            const float endY = safe.back().y;
            if (safe.size() == maxPoints)
                safe.pop_back();
            safe.push_back({1.0f, endY});
        }

        std::lock_guard<std::mutex> lock(mutex_);
        points_.swap(safe);
    }

    // Non-blocking GUI snapshot. The caller must reserve maxPoints first.
    bool tryCopyPoints(std::vector<Point>& destination) const
    {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock())
            return false;
        destination.assign(points_.begin(), points_.end());
        return true;
    }

    // Length is a shape control: 0.5 places point #2 at 25% of the cycle.
    static float lengthPointX(const Point* points, int count, float length) noexcept
    {
        if (count < 3)
            return count >= 2 ? points[1].x : 0.0f;
        const float minimum = points[0].x + 0.002f;
        const float maximum = points[2].x - 0.002f;
        if (maximum < minimum) return points[1].x;
        return std::clamp(length * 0.5f, minimum, maximum);
    }

    // One shared transformation for audio, GUI and regression tests.
    static void applyLegacyControls(Point* points, int count, float length,
                              float attackMs, float releaseMs, double cycleSeconds, float onsetMs = 0.0f) noexcept
    {
        const float cycleMs = static_cast<float>(std::max(0.001, cycleSeconds) * 1000.0);
        if (count >= 5)
        {
            const int attackPoint = count - 2;
            const float attackMin = points[attackPoint - 1].x + 0.002f;
            const float attackMax = points[attackPoint + 1].x - 0.002f;
            if (attackMin <= attackMax)
                points[attackPoint].x = std::clamp(1.0f - attackMs / cycleMs, attackMin, attackMax);
            const float releaseMin = points[1].x + 0.002f;
            const float releaseMax = points[3].x - 0.002f;
            if (releaseMin <= releaseMax)
                points[2].x = std::clamp(releaseMs / cycleMs, releaseMin, releaseMax);
        }
        if (count >= 3)
            points[1].x = lengthPointX(points, count, length);
        if (count >= 2)
            points[0].x = std::clamp(onsetMs / cycleMs, 0.0f,
                                   std::max(0.0f, points[1].x - 0.002f));
    }

    static void applyControls(Point* points, int count, float length,
                              float tailMs, float recoverMs, double cycleSeconds, float onsetMs = 0.0f) noexcept
    {
        applyLegacyControls(points, count, length, tailMs, recoverMs, cycleSeconds, onsetMs);
        if (count < 5) return;
        const float ms = static_cast<float>(std::max(0.001, cycleSeconds) * 1000.0);
        const float recoverMax = count == 5 ? points[4].x - 0.004f : points[3].x - 0.002f;
        const float recoverMin = points[1].x + 0.002f;
        if (recoverMin <= recoverMax)
            points[2].x = std::clamp(recoverMs / ms, recoverMin, recoverMax);
        const int tail = count - 2;
        const float tailMin = points[tail - 1].x + 0.002f;
        const float tailMax = points[tail + 1].x - 0.002f;
        if (tailMin <= tailMax)
            points[tail].x = std::clamp(1.0f - tailMs / ms, tailMin, tailMax);
    }

    float valueAt(float x) const noexcept
    {
        // This function is intentionally not used by the audio thread.
        std::lock_guard<std::mutex> lock(mutex_);
        return sample(points_.data(), static_cast<int>(points_.size()), x, true);
    }

private:
    mutable std::mutex mutex_;
    std::vector<Point> points_;
};
