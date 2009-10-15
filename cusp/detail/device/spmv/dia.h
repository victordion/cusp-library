/*
 *  Copyright 2008-2009 NVIDIA Corporation
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


#pragma once

#include <cusp/dia_matrix.h>

#include <cusp/detail/device/common.h>
#include <cusp/detail/device/utils.h>
#include <cusp/detail/device/texture.h>

namespace cusp
{
namespace detail
{
namespace device
{

////////////////////////////////////////////////////////////////////////
// DIA SpMV kernels 
///////////////////////////////////////////////////////////////////////
//
// Diagonal matrices arise in grid-based discretizations using stencils.  
// For instance, the standard 5-point discretization of the two-dimensional 
// Laplacian operator has the stencil:
//      [  0  -1   0 ]
//      [ -1   4  -1 ]
//      [  0  -1   0 ]
// and the resulting DIA format has 5 diagonals.
//
// spmv_dia
//   Each thread computes y[i] += A[i,:] * x 
//   (the dot product of the i-th row of A with the x vector)
//
// spmv_dia_tex
//   Same as spmv_dia, except x is accessed via texture cache.
//


template <typename IndexType, typename ValueType, unsigned int BLOCK_SIZE, bool UseCache>
__global__ void
spmv_dia_kernel(const IndexType num_rows, 
                const IndexType num_cols, 
                const IndexType num_diagonals,
                const IndexType stride,
                const IndexType * diagonal_offsets,
                const ValueType * values,
                const ValueType * x, 
                      ValueType * y)
{
    __shared__ int offsets[BLOCK_SIZE];
    
    const IndexType thread_id = blockDim.x * blockIdx.x + threadIdx.x;
    const IndexType grid_size = gridDim.x * blockDim.x;

    // load diagonal offsets into shared memory
    if(threadIdx.x < num_diagonals)
        offsets[threadIdx.x] = diagonal_offsets[threadIdx.x];

    __syncthreads();


    for(IndexType row = thread_id; row < num_rows; row += grid_size)
    {
        ValueType sum = y[row];

        IndexType offset = row;

        for(IndexType n = 0; n < num_diagonals; n++)
        {
            const int col = row + offsets[n];
    
            if(col >= 0 && col < num_cols)
            {
                const ValueType A_ij = values[offset];
                sum += A_ij * fetch_x<UseCache>(col, x);
            }
    
            offset += stride;
        }

        y[row] = sum;
    }
}

    
template <bool UseCache, typename IndexType, typename ValueType>
void __spmv_dia(const cusp::dia_matrix<IndexType,ValueType,cusp::device>& dia, 
                const ValueType * x, 
                      ValueType * y)
{
    const unsigned int BLOCK_SIZE = 256;
    const unsigned int MAX_BLOCKS = MAX_THREADS / BLOCK_SIZE;
    const unsigned int NUM_BLOCKS = std::min(MAX_BLOCKS, DIVIDE_INTO(dia.num_rows, BLOCK_SIZE));
    
    if (UseCache)
        bind_x(x);
  
    // the dia_kernel only handles BLOCK_SIZE diagonals at a time
    for(unsigned int base = 0; base < dia.num_diagonals; base += BLOCK_SIZE)
    {
        IndexType num_diagonals = std::min(dia.num_diagonals - base, BLOCK_SIZE);

        spmv_dia_kernel<IndexType, ValueType, BLOCK_SIZE, UseCache> <<<NUM_BLOCKS, BLOCK_SIZE>>>
            (dia.num_rows, dia.num_cols, num_diagonals, dia.stride,
             thrust::raw_pointer_cast(&dia.diagonal_offsets[0]) + base,
             thrust::raw_pointer_cast(&dia.values[0]) + base * dia.stride,
             x, y);
    }

    if (UseCache)
        unbind_x(x);
}

template <typename IndexType, typename ValueType>
void spmv_dia(const cusp::dia_matrix<IndexType,ValueType,cusp::device>& dia, 
              const ValueType * x, 
                    ValueType * y)
{
    __spmv_dia<false>(dia, x, y);
}

template <typename IndexType, typename ValueType>
void spmv_dia_tex(const cusp::dia_matrix<IndexType,ValueType,cusp::device>& dia, 
                  const ValueType * x, 
                        ValueType * y)
{
    __spmv_dia<true>(dia, x, y);
}


template <typename IndexType, typename ValueType>
void spmv(const cusp::dia_matrix<IndexType,ValueType,cusp::device>& dia, 
          const ValueType * x, 
                ValueType * y)
{
    spmv_dia(dia, x, y);
}

template <typename IndexType, typename ValueType>
void spmv_tex(const cusp::dia_matrix<IndexType,ValueType,cusp::device>& dia, 
              const ValueType * x, 
                    ValueType * y)
{
    spmv_dia_tex(dia, x, y);
}

} // end namespace device
} // end namespace detail
} // end namespace cusp

