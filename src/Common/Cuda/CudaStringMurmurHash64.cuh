#pragma once

#include <base/types.h>
#include <Common/Cuda/cudaMurmurHash64.cuh>

template <unsigned int seed = 1>
struct CudaStringMurmurHash64
{
    typedef UInt64 result_type;

    result_type operator()(const char * s, UInt32 len) const { return cudaMurmurHash64(s, len, seed); }
};