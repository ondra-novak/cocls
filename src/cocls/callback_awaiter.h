#pragma once
#ifndef SRC_COCLS_CALLBACK_AWAITER_H_
#define SRC_COCLS_CALLBACK_AWAITER_H_


#include "common.h"
#include "generics.h"

namespace cocls {

///purpose of scoped coroutine is to be held in a scope and it is destroyed when the handle leaves scope
/** Scoped coroutine is best candidate for allocation elision. However because
 * the coroutine is destroyed by the destructor, you must be careful about the coroutine
 * state. You should await to destroy coroutine when it is awaiting for anything. You must
 * send some notification about its finish before you can destroy it;
 */
class scoped_coroutine {
public:
    struct promise_type {
        std::suspend_never initial_suspend() {return {};}
        std::suspend_always final_suspend() noexcept {return {};}
        void return_void() {};
        void unhandled_exception() {}
        scoped_coroutine get_return_object() {
            return scoped_coroutine(std::coroutine_handle<promise_type>::from_promise(*this));
        }
    private:
#ifdef NDEBUG
        void *operator new(std::size_t);
#endif

    };

    scoped_coroutine() = default;
    scoped_coroutine(std::coroutine_handle<promise_type> h):_h(h) {}
    scoped_coroutine(scoped_coroutine &&other):_h(other._h) {other._h = nullptr;}
    scoped_coroutine &operator=(scoped_coroutine &&other) {
        if (this != &other) {
            if (_h) _h.destroy();
            _h = other._h;
            other._h = nullptr;
        }
        return *this;
    }
    ~scoped_coroutine() {
        if (_h) {
            assert("Destroying running coroutine" && _h.done());
            _h.destroy();
        }
    }

protected:
    std::coroutine_handle<promise_type> _h;
};



///registers callback to await a specified awaitable or awaiter.
/**
 *
 * @tparam Awt type of awaitable. You need to specify with & to use reference or
 *  && to pass rvalue reference. Otherwise it is passed by copy or by move (if std::move
 *  is used or rvalue is passed)
 *
 * @param awt awaitable object (or awaiter)
 * @param fn function which is called when await is complete. Note that callback function
 * must have two variants of arguments. If there is an exception, the function
 * is called with std::exception_ptr as an argument
 *
 * @return coroutine which is must not be destroyed before awaitable operation is complete.
 * It can be stored in stack. This also helps to elide allocation
 */
template<typename Awt, typename Fn>
scoped_coroutine callback_await(Awt awt, Fn fn) {
    using RetVal = awaiter_return_value<Awt>;
    try {
        if constexpr(std::is_void_v<RetVal>) {
            co_await awt;
            fn();
        } else {
            fn(co_await awt);
        }
    } catch (...) {
        fn(std::current_exception());
    }
}






}





#endif /* SRC_COCLS_CALLBACK_AWAITER_H_ */
