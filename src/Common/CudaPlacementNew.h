#pragma once

#include <cstddef>

#if defined(__CUDA_ARCH__)
__device__ __forceinline__ void * operator new(std::size_t, void * ptr) noexcept { return ptr; }
__device__ __forceinline__ void * operator new[](std::size_t, void * ptr) noexcept { return ptr; }
__device__ __forceinline__ void operator delete(void *, void *) noexcept { }
__device__ __forceinline__ void operator delete[](void *, void *) noexcept { }
#endif