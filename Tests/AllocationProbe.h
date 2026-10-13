#pragma once

#include <cstddef>

namespace AllocationProbe
{
struct Counts
{
    std::size_t cpp = 0, heap = 0;
};
void begin() noexcept;
Counts end() noexcept;
}
