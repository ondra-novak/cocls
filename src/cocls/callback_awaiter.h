#pragma once
#ifndef SRC_COCLS_CALLBACK_AWAITER_H_
#define SRC_COCLS_CALLBACK_AWAITER_H_


#include "common.h"
#include "generics.h"
#include "alloca_storage.h"
#include "with_allocator.h"
#include "async.h"

namespace cocls {


namespace _details {

template<typename Alloc, typename Awt, typename Fn, typename ... Args>
async<void> callback_await_coro(Alloc &, Fn fn, Args ... args) {
    using RetVal = awaiter_return_value<Awt>;
    Awt awt(std::forward<Args>(args)...);
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

///registers callback to await a specified awaitable or awaiter.
/**
 *
 * @tparam Awt type of awaitable.
 *
 * @param fn function which is called when await is complete. Note that callback function
 * must have two variants of arguments. If there is an exception, the function
 * is called with std::exception_ptr as an argument
 * @param args arguments to construct the awaitable. There are awaitable which are
 *      not move constructible or copy constructible. So you can pass arguments
 *      to construct the awaitable to co_await on it
 */
template<typename Awt, typename Fn, typename ... Args>
void callback_await(Fn &&fn, Args && ... args) {
    default_storage stor;
    _details::callback_await_coro<default_storage, Awt, Fn, Args...>(
        stor, std::forward<Fn>(fn), std::forward<Args>(args)...).detach();
}

///registers callback to await a specified awaitable or awaiter.
/**
 *
 * @tparam Awt type of awaitable.
 * @param alloc reference to an allocator. The allocator must remain valid until
 *  the callback is called
 * @param fn function which is called when await is complete. Note that callback function
 * must have two variants of arguments. If there is an exception, the function
 * is called with std::exception_ptr as an argument
 * @param args arguments to construct the awaitable. There are awaitable which are
 *      not move constructible or copy constructible. So you can pass arguments
 *      to construct the awaitable to co_await on it
 */
template<typename Alloc, typename Awt, typename Fn, typename ... Args>
void callback_await(Alloc &alloc, Fn fn, Args &&... args) {
    _details::callback_await_coro<Alloc, Awt, Fn, Args...>(
        alloc, std::forward<Fn>(fn), std::forward<Args>(args)...).detach();

}

namespace _details {
    template<typename X, typename ... Args>
    struct is_exception_test {
        static constexpr bool val = std::is_base_of_v<std::exception_ptr, std::decay_t<X> >;
    };
}

/// determines, whether argument is (are) exception
/**
 * @retval true argument is exception
 * @retval false argument is not exception
 *
 * @note as the function is constexpr, it can be used int if constexpr()
 */
template<typename ... Args>
inline constexpr bool is_exception(Args && ... x) {
    if constexpr(sizeof...(x) == 1 && _details::is_exception_test<Args...>::val) {
        return true;
    } else {
        return false;
    }
}



}





#endif /* SRC_COCLS_CALLBACK_AWAITER_H_ */
