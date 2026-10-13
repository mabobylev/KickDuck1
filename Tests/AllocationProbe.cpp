#include "AllocationProbe.h"
#include <cstdlib>
#include <new>

namespace
{
thread_local bool recording = false;
thread_local AllocationProbe::Counts counts;
}

extern "C"
{
void* __real_malloc(std::size_t);
void* __real_calloc(std::size_t, std::size_t);
void* __real_realloc(void*, std::size_t);
int __real_posix_memalign(void**, std::size_t, std::size_t);
void* __real_aligned_alloc(std::size_t, std::size_t);

void* __wrap_malloc(std::size_t size)
{
    if (recording) ++counts.heap;
    return __real_malloc(size);
}
void* __wrap_calloc(std::size_t count, std::size_t size)
{
    if (recording) ++counts.heap;
    return __real_calloc(count, size);
}
void* __wrap_realloc(void* pointer, std::size_t size)
{
    if (recording && size > 0) ++counts.heap;
    return __real_realloc(pointer, size);
}
int __wrap_posix_memalign(void** pointer, std::size_t alignment, std::size_t size)
{
    if (recording) ++counts.heap;
    return __real_posix_memalign(pointer, alignment, size);
}
void* __wrap_aligned_alloc(std::size_t alignment, std::size_t size)
{
    if (recording) ++counts.heap;
    return __real_aligned_alloc(alignment, size);
}
}

void* operator new(std::size_t size)
{
    if (recording) ++counts.cpp;
    if (auto* pointer = __real_malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment)
{
    if (recording) ++counts.cpp;
    void* pointer = nullptr;
    if (__real_posix_memalign(&pointer, static_cast<std::size_t>(alignment), size == 0 ? 1 : size) == 0)
        return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void operator delete(void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { std::free(pointer); }

void AllocationProbe::begin() noexcept
{
    counts = {};
    recording = true;
}
AllocationProbe::Counts AllocationProbe::end() noexcept
{
    recording = false;
    return counts;
}
