
#include <cstdio>

#include "cudaCalcMurmurHash64.h"
#include "cudaMurmurHash64.cuh"
#include "cudaReadUnaligned.cuh"

__global__ void kerCalcHash(
    UInt32 str_num,
    char * arr,
    UInt32 * begs,
    bool interpret_as_lengths,
    UInt32 * lens,
    unsigned int seed,
    UInt64 * res_hash)
{
    UInt32 i = blockIdx.x * blockDim.x + threadIdx.x;
    if (!(i < str_num))
        return;

    UInt32 len = lens[i], beg = begs[i];
    if (!interpret_as_lengths)
        --len;

    UInt64 h = cudaMurmurHash64(&(arr[beg]), len, seed);

    /// TODO make it optional
    if (h == 0xFFFFFFFFFFFFFFFF)
        h = 0x0000000000000000;

    res_hash[i] = h;
}

void cudaCalcMurmurHash64(
    UInt32 str_num,
    char * buf,
    bool interpret_as_lengths,
    UInt32 * lens,
    UInt32 * offsets,
    unsigned int seed,
    UInt64 * res_hash,
    cudaStream_t stream)
{
    kerCalcHash<<<(str_num / 256) + 1, 256, 0, stream>>>(str_num, buf, offsets, interpret_as_lengths, lens, seed, res_hash);
}