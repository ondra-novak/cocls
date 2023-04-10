#pragma once
#ifndef _SRC_COCLS_RESUME_HEADER_98YH2ODH983DH293YEDHQ19
#define _SRC_COCLS_RESUME_HEADER_98YH2ODH983DH293YEDHQ19

#include "generics.h"
#include "awaiter.h"
#include <thread>

namespace cocls {


/// Immediately resumes the current coroutine
/**
 * By default, any awaitable which produces co_awaiter or awaiter to await resumes awaiting coroutine into coro_queue where
 * it must wait, until current thread is available to process it. This class constructed as a co_await expression overrides this
 * behaviour. Awaiting coroutine is immediately resumed once the awaited operation is completed interrupting currently running coroutine
 *
 * Usage
 * @code
 * co_await cocls::immediately(async_operation(...));
 * @endcode
 *
 * @note this modificator can be used only with cocls::co_awaiter and variants. It must be supported on the awaiter.
 *
*/
template<typename Awt>
class immediately: public awaiter {
public:
    template<typename Awaitable>
    immediately(Awaitable &&awt):_awt(std::forward<Awt>(awt)) {}
    constexpr bool await_ready() {return _awt.await_ready();}
    constexpr auto await_suspend(std::coroutine_handle<> h) {
        set_handle(h);
        return _awt.await_suspend(&perform_resume, this);
    }
    constexpr decltype(auto) await_resume() {return _awt.await_resume();}
protected:

    static void perform_resume(awaiter *, void *user_ptr, std::coroutine_handle<> &) noexcept {
        immediately *_this = reinterpret_cast<immediately *>(user_ptr);
        std::coroutine_handle<>::from_address(_this->_handle_addr).resume();
    }
    Awt _awt;
};

template<typename T>
immediately(T &&x) -> immediately<decltype(retrieve_awaiter(x))>;



/// Resume coroutine in parallel
/**
 * By default, any awaitable which produces co_awaiter or awaiter to await resumes awaiting coroutine into coro_queue where
 * it must wait, until current thread is available to process it. This class constructed as the co_await expression overrides this
 * behaviour. The resumed coroutine is executed in brand new detached thread, so it continues parallel to the resumer.
 *
 * @note this modificator can be used only with cocls::co_awaiter and variants. It must be supported on the awaiter.
 *
 * Usage
 * @code
 * co_await cocls::parallel(async_operation(...));
 * @endcode
 *
 *
*/
template<typename Awt>
class parallel: public awaiter {
public:
    template<typename Awaitable>
    parallel(Awaitable &&awt):_awt(std::forward<Awt>(awt)) {}
    constexpr bool await_ready() {return _awt.await_ready();}
    constexpr auto await_suspend(std::coroutine_handle<> h) {
        set_handle(h);
        return _awt.await_suspend(&perform_resume, this);
    }
    constexpr decltype(auto) await_resume() {return _awt.await_resume();}
protected:

    static suspend_point<void> perform_resume(awaiter *, void *user_ptr) noexcept {
        parallel *_this = reinterpret_cast<parallel *>(user_ptr);
        auto h = std::coroutine_handle<>::from_address(_this->_handle_addr);
        std::thread t([h]{
            h.resume();
        });
        t.detach();
        return {};
    }
    Awt _awt;

};

template<typename T>
parallel(T &&x) -> parallel<decltype(retrieve_awaiter(x))>;


template<typename T>
auto parallel_resume(suspend_point<T> &&spt) {
    if (!spt.await_ready()) {
            std::thread thr([sp = suspend_point<void>(std::move(spt))]() mutable {
                sp.clear();
            });
            thr.detach();
    }
    return spt.await_resume();
}





}



#endif /*_SRC_COCLS_RESUME_HEADER_98YH2ODH983DH293YEDHQ19*/
