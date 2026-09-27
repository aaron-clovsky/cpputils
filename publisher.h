/*******************************************************************************
* publisher.h
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
* Purpose: Safely publish data from one producing thread to one consuming thread
*          while replacing previously published data with the latest data
*          if it has not yet been consumed
*
* Features: Lock free and retry free, no heap allocation, optionally zero copy
*
* Compile time options:
*     PUBLISHER_USE_EXCEPTIONS <- Throw exceptions when improper usage detected
*     PUBLISHER_NO_EXCEPTIONS  <- Never throw exceptions
*
* Notes:
* - An array of length 3 of the template type T is declared as a member
*   unless true is passed as the second template argument in which case
*   three pointers to buffers of type T must be provided to the constructor
* - publish() + consume() provide the simplest API for small
*   data structures where the copy overhead is acceptable
* - stage() + commit() and reserve() + release() + empty()
*   together allow for zero-copy semantics but must be used carefully
*
* Regarding exceptions:
* - By default this header enables exceptions only in DEBUG mode
* - Exceptions are not meant to be recoverable, they indicate a bug
* - If *ONLY* the publish(), consume() and reset() methods are used then
*   exceptions will never occur regardless of compile time options
* - When exceptions are disabled publish() will return false only
*   if a consumer has successfully called reserve() twice in a row without
*   properly calling release()
* - When exceptions are enabled publish() will only return true
*******************************************************************************/
#ifndef PUBLISHER_H
#define PUBLISHER_H

/*******************************************************************************
Compile time options
*******************************************************************************/
#if (!defined(NDEBUG) || defined(PUBLISHER_USE_EXCEPTIONS)) \
    && !defined(PUBLISHER_NO_EXCEPTIONS)
    #define PUBLISHER_COND_NOEXCEPT
    #undef PUBLISHER_USE_EXCEPTIONS
    #define PUBLISHER_USE_EXCEPTIONS
#else
    #define PUBLISHER_COND_NOEXCEPT noexcept
#endif

/*******************************************************************************
Macros
*******************************************************************************/
#if (defined(_MSVC_LANG) && _MSVC_LANG < 201103L) \
    || (!defined(_MSVC_LANG) && __cplusplus < 201103L)
    #error "C++11 or newer is required"
#elif defined(_MSVC_LANG) && _MSVC_LANG >= 202002L
    #define PUBLISHER_CPP20
    #define PUBLISHER_CPP17
#elif defined(_MSVC_LANG) && _MSVC_LANG >= 201703L
    #define PUBLISHER_CPP17
#endif

/*******************************************************************************
Headers
*******************************************************************************/
#include <atomic>

#ifdef PUBLISHER_CPP20
    #include <bit>
#elif defined(_MSC_VER)
    #include <intrin.h>
#endif

#ifdef PUBLISHER_USE_EXCEPTIONS
    #include <stdexcept>
#endif

/*******************************************************************************
Types
*******************************************************************************/
template <typename T, bool ExternalBuffer>
struct publisher_buffers;

template <typename T>
struct publisher_buffers<T, false>
{
    T buff[3];
    T* buffer(unsigned i) { return &buff[i]; }
};

template <typename T>
struct publisher_buffers<T, true>
{
    T* buff[3];
    publisher_buffers(T* a, T* b, T* c) : buff{ a, b, c } {}
    T* buffer(unsigned i) { return buff[i]; }
};

template <typename T, bool ExternalBuffer = false>
class publisher : private publisher_buffers<T, ExternalBuffer>
{
    using Base = publisher_buffers<T, ExternalBuffer>;

    std::atomic<unsigned> map;
    std::atomic<T*> latest;
    T* recycle;

    #ifdef PUBLISHER_USE_EXCEPTIONS
    std::atomic<bool> stage_flag{ false };
    std::atomic<bool> reserve_flag{ false };
    #endif

    static unsigned ctz(unsigned n) noexcept {
        #ifdef PUBLISHER_CPP20
        return std::countr_zero(n);
        #elif defined(__GNUC__) || defined(__clang__)
        return __builtin_ctz(n);
        #elif defined(_MSC_VER)
        unsigned long index;
        (void)_BitScanForward(&index, n);
        return index;
        #else
        return (0x1213 >> ((n & 7) << 1)) & 3;
        #endif
    }

    void clear(unsigned idx) noexcept {
        (void)map.fetch_and(~(1 << idx));
    }

    void set(unsigned idx) noexcept {
        (void)map.fetch_or(1 << idx);
    }

    T* alloc() noexcept {
        unsigned idx = ctz(map);
        if (idx > 2) return nullptr;

        clear(idx);

        return this->buffer(idx);
    }

    void dealloc(T* const ptr) noexcept {
        unsigned idx;

        if (ptr == this->buffer(0)) idx = 0;
        else if (ptr == this->buffer(1)) idx = 1;
        else if (ptr == this->buffer(2)) idx = 2;
        else return;

        set(idx);
    }

public:
    template <bool E = ExternalBuffer, typename std::enable_if<!E, int>::type = 0>
    publisher() : Base() { reset(); }

    template <bool E = ExternalBuffer, typename std::enable_if<E, int>::type = 0>
    publisher(T* a, T* b, T* c) : Base(a, b, c) { reset(); }

    publisher(const publisher&) = delete;
    publisher& operator=(const publisher&) = delete;
    publisher(publisher&&) = delete;
    publisher& operator=(publisher&&) = delete;

    void reset() noexcept {
        map = 15;
        latest = nullptr;
        recycle = nullptr;

        #ifdef PUBLISHER_USE_EXCEPTIONS
        stage_flag.store(false);
        reserve_flag.store(false);
        #endif
    }

    T* stage() PUBLISHER_COND_NOEXCEPT {
        T* allocated;

        #ifdef PUBLISHER_USE_EXCEPTIONS
        if (stage_flag.exchange(true)) {
            throw std::runtime_error("stage() called before commit()");
        }
        #endif

        if (recycle) {
            allocated = recycle;
            recycle = nullptr;
        }
        else {
            allocated = alloc();
        }

        return allocated;
    }

    void commit(T* const staged) PUBLISHER_COND_NOEXCEPT {
        #ifdef PUBLISHER_USE_EXCEPTIONS
        if (!stage_flag.exchange(false)) {
            throw std::runtime_error("commit() called before stage()");
        }
        #endif

        recycle = latest.exchange(staged, std::memory_order_acq_rel);
    }

    T* reserve() PUBLISHER_COND_NOEXCEPT {
        T* const result = latest.exchange(nullptr, std::memory_order_acquire);

        #ifdef PUBLISHER_USE_EXCEPTIONS
        if (result ? reserve_flag.exchange(true) : reserve_flag.load()) {
            throw std::runtime_error("reserve() called before release()");
        }
        #endif

        return result;
    }

    void release(T* reserved) PUBLISHER_COND_NOEXCEPT {
        #ifdef PUBLISHER_USE_EXCEPTIONS
        if (!reserve_flag.exchange(false)) {
            throw std::runtime_error("release() called before reserve()");
        }
        #endif

        dealloc(reserved);
    }

#ifdef PUBLISHER_CPP17
    [[maybe_unused]]
#endif
    bool empty() noexcept {
        return !latest.load(std::memory_order_acquire);
    }

#ifdef PUBLISHER_CPP17
    [[maybe_unused]]
#endif
    bool publish(const T* const outbound) PUBLISHER_COND_NOEXCEPT {
        T* allocated;
        if (!outbound) return false;

        if ((allocated = stage())) {
            *allocated = *outbound;
            commit(allocated);
            return true;
        }

        return false;
    }

#ifdef PUBLISHER_CPP17
     [[maybe_unused]]
#endif
    bool consume(T* const inbound) PUBLISHER_COND_NOEXCEPT {
        T* reserved;
        if (!inbound) return false;

        if ((reserved = reserve())) {
            *inbound = *reserved;
            release(reserved);
            return true;
        }

        return false;
    }
};

#endif // PUBLISHER_H
