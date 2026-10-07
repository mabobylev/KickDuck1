#include "DuckingCurve.h"

#include <cmath>
#include <utility>

namespace
{
    std::shared_ptr<const std::vector<DuckingCurve::Point>> loadPoints(
        const std::shared_ptr<const std::vector<DuckingCurve::Point>>& p) noexcept
    {
        return std::atomic_load_explicit(&p, std::memory_order_acquire);
    }

    void storePoints(
        std::shared_ptr<const std::vector<DuckingCurve::Point>>& destination,
        std::shared_ptr<const std::vector<DuckingCurve::Point>> value) noexcept
    {
        std::atomic_store_explicit(
            &destination,
            std::move(value),
            std::memory_order_release);
    }
}

DuckingCurve::DuckingCurve()
{
    auto initial = std::make_shared<std::vector<Point>>();

    initial->push_back({ 0.00f, 0.00f });
    initial->push_back({ 0.06f, 0.00f });
    initial->push_back({ 0.18f, 0.85f });
    initial->push_back({ 0.48f, 0.72f });
    initial->push_back({ 0.82f, 0.28f });
    initial->push_back({ 1.00f, 0.00f });

    storePoints(points, std::move(initial));
}

float DuckingCurve::clamp01(float value)
{
    if (!std::isfinite(value))
        return 0.0f;

    return std::clamp(value, 0.0f, 1.0f);
}

void DuckingCurve::sanitise(std::vector<Point>& p)
{
    for (auto& point : p)
    {
        if (!std::isfinite(point.x)) point.x = 0.0f;
        if (!std::isfinite(point.y)) point.y = 0.0f;

        point.x = clamp01(point.x);
        point.y = clamp01(point.y);
    }

    std::sort(
        p.begin(),
        p.end(),
        [](const Point& a, const Point& b)
        {
            return a.x < b.x;
        });

    // Never publish an empty curve. valueAt() relies on this invariant.
    if (p.empty())
    {
        p.push_back({ 0.0f, 0.0f });
        p.push_back({ 1.0f, 0.0f });
        return;
    }

    // Always keep explicit endpoints so the interpolation is well-defined.
    if (p.front().x > 0.0f)
        p.insert(p.begin(), { 0.0f, p.front().y });
    else
        p.front().x = 0.0f;

    if (p.back().x < 1.0f)
        p.push_back({ 1.0f, p.back().y });
    else
        p.back().x = 1.0f;

    // Remove duplicate x positions. Keep the later point, which corresponds
    // to the most recent GUI edit after sorting.
    std::vector<Point> unique;
    unique.reserve(p.size());

    for (const auto& point : p)
    {
        if (!unique.empty() &&
            std::abs(unique.back().x - point.x) < 0.000001f)
        {
            unique.back() = point;
        }
        else
        {
            unique.push_back(point);
        }
    }

    if (unique.size() < 2)
        unique.push_back({ 1.0f, unique.front().y });

    p = std::move(unique);
}

float DuckingCurve::valueAt(float x) const
{
    const auto snapshot = loadPoints(points);

    if (!snapshot || snapshot->empty())
        return 0.0f;

    const auto& p = *snapshot;
    x = clamp01(x);

    if (p.size() == 1)
        return clamp01(p.front().y);

    if (x <= p.front().x)
        return clamp01(p.front().y);

    if (x >= p.back().x)
        return clamp01(p.back().y);

    std::size_t segment = 1;
    while (segment < p.size() && x > p[segment].x)
        ++segment;

    if (segment >= p.size())
        return clamp01(p.back().y);

    const auto& p1 = p[segment - 1];
    const auto& p2 = p[segment];
    const auto& p0 = (segment >= 2) ? p[segment - 2] : p1;
    const auto& p3 = (segment + 1 < p.size()) ? p[segment + 1] : p2;

    const float dx = p2.x - p1.x;
    if (dx <= 0.000001f)
        return clamp01(p2.y);

    const float t = std::clamp((x - p1.x) / dx, 0.0f, 1.0f);
    const float t2 = t * t;
    const float t3 = t2 * t;

    // Centripetal-like Catmull-Rom in x/y parameter space.  The result is
    // clamped because the curve represents a ducking amount, not an
    // unrestricted mathematical spline.  The GUI draws exactly this shape.
    const float m1 = 0.5f * (p2.y - p0.y);
    const float m2 = 0.5f * (p3.y - p1.y);

    const float value =
        (2.0f * t3 - 3.0f * t2 + 1.0f) * p1.y
        + (t3 - 2.0f * t2 + t) * m1
        + (-2.0f * t3 + 3.0f * t2) * p2.y
        + (t3 - t2) * m2;

    return clamp01(value);
}

std::vector<DuckingCurve::Point> DuckingCurve::getPoints() const
{
    const auto snapshot = loadPoints(points);

    if (!snapshot)
        return { { 0.0f, 0.0f }, { 1.0f, 0.0f } };

    return *snapshot;
}

void DuckingCurve::setPoints(const std::vector<Point>& newPoints)
{
    auto next = std::make_shared<std::vector<Point>>(newPoints);
    sanitise(*next);
    storePoints(points, std::move(next));
}

void DuckingCurve::setPoint(std::size_t index, float x, float y)
{
    const auto snapshot = loadPoints(points);
    auto next = std::make_shared<std::vector<Point>>(
        snapshot ? *snapshot : std::vector<Point>{ { 0.0f, 0.0f }, { 1.0f, 0.0f } });

    if (index >= next->size())
        return;

    (*next)[index].x = clamp01(x);
    (*next)[index].y = clamp01(y);
    sanitise(*next);
    storePoints(points, std::move(next));
}

void DuckingCurve::addPoint(float x, float y)
{
    const auto snapshot = loadPoints(points);
    auto next = std::make_shared<std::vector<Point>>(
        snapshot ? *snapshot : std::vector<Point>{ { 0.0f, 0.0f }, { 1.0f, 0.0f } });

    next->push_back({ clamp01(x), clamp01(y) });
    sanitise(*next);
    storePoints(points, std::move(next));
}

void DuckingCurve::removePoint(std::size_t index)
{
    const auto snapshot = loadPoints(points);
    auto next = std::make_shared<std::vector<Point>>(
        snapshot ? *snapshot : std::vector<Point>{ { 0.0f, 0.0f }, { 1.0f, 0.0f } });

    // Keep at least two points and never remove the endpoints.
    if (next->size() <= 2 || index == 0 || index + 1 >= next->size())
        return;

    next->erase(next->begin() + static_cast<std::ptrdiff_t>(index));
    sanitise(*next);
    storePoints(points, std::move(next));
}

std::size_t DuckingCurve::size() const noexcept
{
    const auto snapshot = loadPoints(points);
    return snapshot ? snapshot->size() : 0;
}
