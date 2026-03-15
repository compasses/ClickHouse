#include <dlfcn.h>
#include <sched.h>
#include <cuda_runtime.h>
#include <iostream>
#include <Common/Cuda/CudaHostPinnedMemPool.h>
#include <Common/Cuda/cudaInitDevice.h>
 #include <sys/mman.h>

void cudaInitDevice(int dev_number, size_t pinned_pool_size)
{
    int device_count = 0;
    cudaError_t err = cudaGetDeviceCount(&device_count);
    if (err != cudaSuccess)
    {
        std::cerr << "CUDA initialization skipped: cudaGetDeviceCount failed: " << cudaGetErrorString(err) << "\n";
        return;
    }
    if (device_count == 0)
    {
        std::cerr << "CUDA initialization skipped: No CUDA devices found.\n";
        return;
    }
    if (dev_number >= device_count)
    {
        std::cerr << "CUDA initialization skipped: Requested CUDA device " << dev_number << " but only " << device_count << " devices found.\n";
        return;
    }

    err = cudaSetDevice(dev_number);
    if (err != cudaSuccess)
    {
        std::cerr << "CUDA initialization skipped: cudaSetDevice(" << dev_number << ") failed: " << cudaGetErrorString(err) << "\n";
        return;
    }

    std::cerr << "CUDA initialization SUCCESS for device " << dev_number << ".\n";

    if (pinned_pool_size > 0)
    {
        try
        {
            CudaHostPinnedMemPool::instance().init(pinned_pool_size);
        }
        catch (const std::exception & e)
        {
            std::cerr << "CUDA initialization skipped: CudaHostPinnedMemPool init failed: " << e.what() << "\n";
        }
    }
}
