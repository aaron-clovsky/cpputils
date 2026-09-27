/*******************************************************************************
* dispatch_thread.h
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
* Purpose: Provides a simple mechanism for threading functions
*
* Notes:
* - The same thread that called start() must call stop()/try_stop*()
* - Repeated calls to start()/stop()/try_stop*() are safe
*******************************************************************************/
#pragma once

/*******************************************************************************
Headers
*******************************************************************************/
#include <sane_binary_semaphore.h>
#include <publisher.h>
#include <thread>
#include <atomic>
#include <type_traits>

/*******************************************************************************
dispatch_thread
*******************************************************************************/
template <typename T, typename Action>
class dispatch_thread {
    using publisher_type = std::conditional_t<std::is_void_v<T>, void*, publisher<T>*>;
    using clock = std::chrono::steady_clock;

    Action action;
    publisher_type thread_publisher;
    sane_binary_semaphore execute_sem{ 0 };
    sane_binary_semaphore stop_sem{ 0 };
    std::jthread thread;
    std::atomic<bool> start_state{ false };
    std::atomic_flag wait_flag;
    std::atomic_flag start_flag;
    std::atomic<clock::duration::rep> run_ticks{ 0 };
    std::atomic<clock::duration::rep> execute_ticks{ 0 };

    #ifdef _MSC_VER
    __declspec(noinline)
    #endif
    void exception_wrapper(auto& callback) const noexcept {
        #ifdef NDEBUG
            #ifdef _MSC_VER
            __try {
                callback();
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                std::terminate();
            }
            #else
            try {
                callback();
            }
            catch (...) {
                std::terminate();
            }
            #endif
        #else
            callback();
        #endif
    }

    void thread_main(std::stop_token stoken) noexcept {
        std::stop_callback callback_stop(stoken, [this] { execute_sem.release(); });
        
        run_ticks.store(clock::now().time_since_epoch().count(), std::memory_order_release);

        while (true) {
            (void)wait_flag.test_and_set();
            execute_sem.acquire();
            execute_ticks.store(clock::now().time_since_epoch().count(), std::memory_order_release);
            wait_flag.clear();
            if (stoken.stop_requested()) break;

            if constexpr (std::is_void_v<T>) {
                exception_wrapper(action);
            }
            else {
                auto* active_publisher = static_cast<publisher<T>*>(thread_publisher);
                T* value = active_publisher->reserve();
                if (value == nullptr) continue;

                auto wrapper = [this, value]() { action(*value); };
                exception_wrapper(wrapper);

                active_publisher->release(value);
            }
        }

        stop_sem.release();
        start_state.store(false);
    }

public:
    explicit dispatch_thread(publisher<T>& p, Action a) requires (!std::is_void_v<T>)
        : thread_publisher(&p), action(std::move(a)) { }

    explicit dispatch_thread(Action a) requires (std::is_void_v<T>)
        : thread_publisher(nullptr), action(std::move(a)) { }
    
    bool start() {
        if (bool expected = false; start_state.compare_exchange_strong(expected, true)) {
            execute_sem.try_acquire();
            (void)start_flag.test_and_set();
            try {
                thread = std::jthread([this](std::stop_token s) { thread_main(std::move(s)); });
            }
            catch (...) {
                start_flag.clear();
                start_state.store(false);
                throw;
            }
            return true;
        }
       return false;
    }

    bool stop() {
        if (bool expected = true; start_state.compare_exchange_strong(expected, false)) {
            thread = std::jthread();
            start_flag.clear();
            return true;
        }
        return false;
    }

    bool running() const noexcept { return start_flag.test(); }

    bool waiting() const noexcept { return wait_flag.test(); }

    std::optional<clock::duration> execute_duration() const noexcept {
        if (waiting()) return std::nullopt;
        
        clock::time_point start_time{ clock::duration{ execute_ticks.load(std::memory_order_acquire)} };

        return clock::now() - start_time;
    }

    std::optional<clock::duration> run_duration() const noexcept {
        if (!running()) return std::nullopt;

        clock::time_point start_time{ clock::duration{ run_ticks.load(std::memory_order_acquire)} };

        return clock::now() - start_time;
    }

    bool execute() noexcept {
        if (!running()) return false;
        execute_sem.release();
        return true;
    }

    bool request_stop() {
        if (!running()) return true;
        thread.request_stop();
    }

    bool try_stop() { 
        if (!running()) return true;
        thread.request_stop();
        if (!stop_sem.try_acquire()) return false;
        stop_sem.release();
        stop();
        return true;
    }

    template<class Rep, class Period>
    bool try_stop_for(const std::chrono::duration<Rep, Period>& rel_time) {
        if (!running()) return true;
        thread.request_stop();
        if (!stop_sem.try_acquire_for(rel_time)) return false;
        stop_sem.release();
        stop();
        return true;
    }

    template<class Clock, class Duration>
    bool try_stop_until(const std::chrono::time_point<Clock, Duration>& abs_time) {
        if (!running()) return true;
        thread.request_stop();
        if (!stop_sem.try_acquire_until(abs_time)) return false;
        stop_sem.release();
        stop();
        return true;
    }
};

template <typename T, typename Action>
dispatch_thread(publisher<T>&, Action) -> dispatch_thread<T, Action>;

template <typename Action>
dispatch_thread(Action) -> dispatch_thread<void, Action>;
