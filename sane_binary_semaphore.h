/*******************************************************************************
* sane_binary_semaphore.h
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
*
* Purpose: Like C++20's std::binary_semaphore but an actual binary semaphore
*          that enforces the maximum value of one, because it's binary...
* Notes:
* - Marking acquire() and release() noexcept isn't standard but MSVC & GCC
*   mark as such already while Clang's implementation doesn't throw anyway
* - In C++26 (P2643) atomic_flag alone should be able to do all of this
*******************************************************************************/
#ifndef SANE_BINARY_SEMAPHORE_H
#define SANE_BINARY_SEMAPHORE_H

/*******************************************************************************
Headers
*******************************************************************************/
#include <semaphore>
#include <atomic>
#include <cstddef>
#include <chrono>

/*******************************************************************************
sane_binary_semaphore
*******************************************************************************/
class sane_binary_semaphore {
    std::binary_semaphore sem;
    std::atomic_flag state;

public:
    explicit sane_binary_semaphore(std::ptrdiff_t desired = 0) : sem(desired) {
        if (desired != 0) (void)state.test_and_set();
    }

    static constexpr std::ptrdiff_t max() noexcept {
        return 1;
    }

    void acquire() noexcept {
        sem.acquire();
        state.clear();
    }

    bool try_acquire() noexcept {
        if (sem.try_acquire()) {
            state.clear();
            return true;
        }
        return false;
    }

    template<class Rep, class Period>
    bool try_acquire_for(const std::chrono::duration<Rep, Period>& rel_time) {
        if (sem.try_acquire_for(rel_time)) {
            state.clear();
            return true;
        }
        return false;
    }

    template<class Clock, class Duration>
    bool try_acquire_until(const std::chrono::time_point<Clock, Duration>& abs_time) {
        if (sem.try_acquire_until(abs_time)) {
            state.clear();
            return true;
        }
        return false;
    }

    void release() noexcept {
        if (!state.test_and_set()) sem.release();
    }
};

#endif
