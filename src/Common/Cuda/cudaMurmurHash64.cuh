#pragma once

#include <base/types.h>
#include <Common/Cuda/cudaReadUnaligned.cuh>

inline __device__ UInt64 cudaMurmurHash64(const char * s, UInt32 len, unsigned int seed)
{
    const UInt64 m = 0xc6a4a7935bd1e995;
    const int r = 47;

    UInt64 h = seed ^ (len * m);

    const char * data = s;
    const char * end = data + len;
    bool is_first_read = true;
    UInt64 tmp_buf;

    while (data != end)
    {
        bool is_complete_uint64 = (end - data >= 8);
        UInt64 k = cudaReadStringUnaligned64(is_first_read, tmp_buf, data, end);

        if (is_complete_uint64)
        {
            k *= m;
            k ^= k >> r;
            k *= m;
        }

        h ^= k;
        h *= m;
    }

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}