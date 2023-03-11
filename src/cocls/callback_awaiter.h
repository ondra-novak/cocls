#pragma once
#ifndef SRC_COCLS_CALLBACK_AWAITER_H_
#define SRC_COCLS_CALLBACK_AWAITER_H_


#include "common.h"
#include "generics.h"
#include "alloca_storage.h"
#include "with_allocator.h"
#include "async.h"

namespace cocls {


///Instance of this class  is passed to the callback when callback_await is used
/**
 * @tparam T expected type returned by the await operation. Can be void
 *
 * The class has strong connection with the callback_await and without it doesn't
 * work properly. It handles exceptional state, when awaitable operation throws exception.
 * In this case, the object can rethrow exception it is used to access the value.
 */
template<typename T>
struct await_result { // @suppress("Miss copy constructor or assignment operator")
    T *value = nullptr;

    ///Retrieve the value
    /**
     * @return carried value
     * @exception any throws current exception if the state is exceptional
     */
    T &get() const {
        if (!value) throw;
        return *value;
    }

    ///Retrieve the value
    /**
     * @return carried value
     * @exception any throws current exception if the state is exceptional
     */
    T &operator *() const {
        return get();
    }

    ///Tests whether object is in exceptional state.
    bool operator!() const {return !value;}
    ///Tests whether object has value
    operator bool () const {return static_cast<bool>(value);}
};


template<>
struct await_result<void> {
    bool is_valid = false;

    void get() const {
        if (!is_valid) throw;
    }

    bool operator!() const {return !is_valid;}
    operator bool () const {return static_cast<bool>(is_valid);}
};

namespace _details {


template<typename Alloc, typename Awt, typename Fn, typename ... Args>
async<void> callback_await_coro(Alloc &, Fn fn, Args ... args) noexcept {
    using RetVal = std::decay_t<awaiter_return_value<Awt> >;
    Awt awt(std::forward<Args>(args)...);
    try {
        if constexpr(std::is_void_v<RetVal>) {
            co_await awt;
            fn(await_result<void>{true});
        } else {
            fn(await_result<RetVal>{&co_await awt});
        }
    } catch (...) {
        fn(await_result<RetVal>{});
    }
}

}



///registers callback to await a specified awaitable or awaiter.
/**
 *
 * @tparam Awt type of awaitable.
 *
 * @param fn function which is called when await is complete. The function accepts
 * await_result<T> where T is type of result
 *
 *  @code
 *  auto callback = [](await_result<T> result) {
 *      try {
 *          use_result(*result);
 *      } catch (std::exception &e) {
 *          //handle exception
 *      }
 *  };
 *  @endcode
 *
 * @param args arguments to construct the awaitable. There are awaitable which are
 *      not move constructible or copy constructible. So you can pass arguments
 *      to construct the awaitable to co_await on it
 *
 *
 */
template<typename Awt, typename Fn, typename ... Args>
CXX20_REQUIRES(std::invocable<Fn, await_result<std::decay_t<awaiter_return_value<Awt> > > >)
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
 *
 * @param fn function which is called when await is complete. The function accepts
 * await_result<T> where T is type of result
 *
 *  @code
 *  auto callback = [](await_result<T> result) {
 *      try {
 *          use_result(*result);
 *      } catch (std::exception &e) {
 *          //handle exception
 *      }
 *  };
 *  @endcode
 *
 * @param args arguments to construct the awaitable. There are awaitable which are
 *      not move constructible or copy constructible. So you can pass arguments
 *      to construct the awaitable to co_await on it
 */
template<typename Alloc, typename Awt, typename Fn, typename ... Args>
CXX20_REQUIRES(std::invocable<Fn, await_result<std::decay_t<awaiter_return_value<Awt> > > >)
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



}





#endif /* SRC_COCLS_CALLBACK_AWAITER_H_ */
