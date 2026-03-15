#pragma once

#include <cstdint>
#include <cuda.h>
#include <cuda_runtime.h>

#include <base/types.h>

void cudaCalcCityHash64(
    UInt32 str_num,
    char * buf,
    bool interpret_as_lengths,
    UInt32 * lens,
    UInt32 * offsets,
    UInt64 * res_hash,
    cudaStream_t stream = 0);
