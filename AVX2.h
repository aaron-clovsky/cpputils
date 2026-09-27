/*******************************************************************************
* AVX2.h
* Copyright (c) 2026 Aaron Clovsky
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of
* this software and associated documentation files (the "Software"), to deal in
* the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is furnished to do
* so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*******************************************************************************/
#ifndef AVX2_H
#define AVX2_H

/*******************************************************************************
Headers
*******************************************************************************/
#include <immintrin.h>

/*******************************************************************************
Macros
*******************************************************************************/
#if defined(__GNUC__) || defined(__clang__)
    #define AVX2_TARGET __attribute__((target("avx2")))
#else
    #define AVX2_TARGET
#endif

/*******************************************************************************
AVX2
*******************************************************************************/
class AVX2
{
private:
    static bool is_supported() {
        #if defined(__GNUC__) || defined(__clang__)
        return __builtin_cpu_supports("avx2");
        #elif defined(_MSC_VER)
        int info[4];
        __cpuidex(info, 7, 0);
        return (info[1] & 32) != 0;
        #endif
    }

public:
    AVX2() = delete;
    static inline bool supported = is_supported();
};

#endif
