/*
 * generics.h
 *
 *  Created on: 8. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLS_GENERICS_H_
#define SRC_COCLS_GENERICS_H_

#include <type_traits>

namespace cocls {

namespace _details {

    template<typename X>
    auto test_has_co_await(X &&x) -> decltype(x.operator co_await());
    std::nullptr_t test_has_co_await(...);

    template<typename X>
    auto test_can_co_await(X &&x) -> decltype(operator co_await(std::forward<X>(x)));
    std::nullptr_t test_can_co_await(...);

}

template<typename X>
inline constexpr bool has_co_await = !std::is_same_v<std::nullptr_t, decltype(_details::test_has_co_await(std::declval<X>()))>;
template<typename X>
inline constexpr bool has_global_co_await = !std::is_same_v<std::nullptr_t, decltype(_details::test_can_co_await(std::declval<X>()))> ;


///Wraps existing awaiter to new awaiter. Original awaiter is stored as reference
/**
 * @tparam Awt original awaiter
 *
 * This is used for await_transform(), to avoid original awaiter copying. The
 * await_transform can return awaiter_wrapper instead original awaiter.
 */
template<typename Awt>
class awaiter_wrapper {
public:
    awaiter_wrapper (Awt &owner):_owner(owner) {}
    constexpr bool await_ready() {return _owner.await_ready();}
    constexpr auto await_suspend(std::coroutine_handle<> h) {return _owner.await_suspend(h);}
    constexpr decltype(auto) await_resume() {return _owner.await_resume();}
protected:
    Awt &_owner;
};

template<typename T>
awaiter_wrapper(T &) -> awaiter_wrapper<T>;


///retrieves awaiter from awaitable
/**
 * Implements FINAE rules how awaiter is retrieved from awaitable.
 * Note that if the awaitable is also awaiter, function returns awaiter_wrapper,
 * because we need to always construct an object
 *
 * @param obj awaitable object
 * @return awaiter
 */
template<typename Object>
auto retrieve_awaiter(Object &&obj) {
    if constexpr(has_co_await<Object>) {
        return obj.operator co_await();
    } else if constexpr(has_global_co_await<Object>) {
        return operator co_await(obj);
    } else {
        return awaiter_wrapper<Object>(obj);
    }
}

///contains return value of awaiter or awaitable of type T
/**
 * it extracts awaiter, if there is available co_await operator.
 */
template<typename T>
using awaiter_return_value = decltype(retrieve_awaiter(std::declval<T>()).await_resume());



}



#endif /* SRC_COCLS_GENERICS_H_ */
