/*
 * shared_future.h
 *
 *  Created on: 8. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLS_SHARED_FUTURE_H_
#define SRC_COCLS_SHARED_FUTURE_H_

#include "future.h"

namespace cocls {




///Shared future works similar as future<> but can be moved or copied, because it is ref-counter shared place
/**
 * @tparam T type of value
 *
 * The instance of shared_future don't need to be returned from the function, as there is still
 * way how to convert return value from from future to shared_future.
 *
 * To construct such future, you need to pass function which returns the future<> to the constructor
 * of shared_future<>. You can use std::bind
 *
 * @code
 * std::shared_future f(std::bind(&do_work, a, b, c));
 * @endcode
 *
 * the above function calls do_work(a,b,c) and converts returned future to shared_future
 *
 * The shared future uses heap to allocate the shared state. It can be awaited by multiple
 * awaiters (each must holds its reference to the instance). It is also possible to
 * remove all reference before the future is marked ready. While the future is
 * pending, an extra reference is counted. Once the future is set ready, this reference
 * is removed and if it is last reference, the shared state is destroyed.
 *
 * Once the shared_future is ready, its content acts as ordinary variable. Anyone who
 * holds reference can read or even modify the content. There is no extra synchronization
 * for these actions, so they are not probably MT safe. This allows to move out the content
 * or read content by multiple readers.
 *
 *
 */
template<typename T, typename Base = cocls::future<T> >
class shared_future {


    class future_internal;

    class resolve_cb: public abstract_awaiter {
    public:
        void charge(std::shared_ptr<future_internal> ptr);
        virtual void resume() noexcept override {
            _ptr = nullptr;
        }
    protected:
        std::shared_ptr<Base> _ptr;
    };


    class future_internal: public Base {
    public:
        using future<T>::future;



        resolve_cb resolve_tracer;
    };

public:

    ///construct uninitialized object
    /**
     * Construct will not initialize the shared state because in most of cases
     * the future object is later replaced or assigned (which is allowed).
     *
     * So any copying of shared_future before initialization doesn't mean that
     * instance is shared.
     *
     * If you need to initialize the object, call init_if_needed() or get_promise()
     */
    shared_future() = default;

    ///Construct shared future retrieve promise
    /**
     * @param fn function which retrieve promise, similar to future() constructor.
     * Your future is initialized and your promise can be used to resolve the future
     *
     */
    template<typename Fn> CXX20_REQUIRES(std::invocable<Fn, promise<T> >)
    shared_future(Fn &&fn)
        :_ptr(std::make_shared<future_internal>(std::forward<Fn>(fn))) {

        _ptr->resolve_tracer.charge(_ptr);
    }


    ///Construct shared future from normal future
    /**
     * Only way, how to construct this future is to pass a function/lambda function or bound
     * function to the constructor, which is called during construction. Function must return
     * future<T>
     *
     * @param fn function to be called and return future<T>
     */
    template<typename Fn> CXX20_REQUIRES(std::same_as<decltype(std::declval<Fn>()()), Base > )
    shared_future(Fn &&fn)
        :_ptr(std::make_shared<future_internal>()) {
        _ptr->result_of(std::forward<Fn>(fn));
        if (_ptr->pending()) _ptr->resolve_tracer.charge(_ptr);
    }

    ///Construct shared future from future_coro
    /**
     * Starts coroutine and initializes shared_future. Result of coroutine is used to resolve
     * the future
     * @param coro coroutine result
     */
    template<typename _Policy>
    shared_future(future_coro<T, _Policy> &&coro)
        :_ptr(std::make_shared<future_internal>(std::move(coro))) {
        _ptr->resolve_tracer.charge(_ptr);
    }

    ///Retrieve the future itself
    /** retrieved as reference, can't be copied */
    operator Base &() {return *_ptr;};


    ///return resolved future
    template<typename ... Args>
    static shared_future<T> set_value(Args && ... args) {
        return shared_future([&]{return Base::set_value(std::forward<Args>(args)...);});
    }

    ///return resolved future
    static shared_future<T> set_exception(std::exception_ptr e) {
        return shared_future([&]{return Base::set_exception(std::move(e));});
    }

    ///initializes object if needed, otherwise does nothing
    void init_if_needed() {
        if (_ptr) _ptr = std::make_shared<future_internal>();
    }

    ///retrieves promise from unitialized shared_future.
    /**
     * Function initializes the future, then returns promise. You can retrieve only
     * one promise
     * @return promise object
     */
    auto get_promise() {
        init_if_needed();
        auto p = _ptr->get_promise();
        _ptr->resolve_tracer.charge(_ptr);
        return p;
    }

    ///Determines, whether future is ready
    bool ready() const {
        if (_ptr) return _ptr->ready();
        else return false;
    }

    ///Retrieves value
    decltype(auto) value() {
        if (_ptr) return _ptr->value();
        else throw value_not_ready_exception();;
    }

    ///Wait synchronously
    /**
     * @return the value of the future
     */
    decltype(auto) wait() {
        return _ptr->wait();
    }


    ///For compatible API - same as wait()
    decltype(auto) join() {
        _ptr->wait();
    }

    ///Synchronise with the future, but doesn't pick the value
    /** Just waits for result, but doesn't pick the result including the exception */
    void sync() {
        _ptr->sync();
    }

    ///co_await the result.
    auto operator co_await() {return _ptr->operator co_await();}

protected:
    std::shared_ptr<future_internal> _ptr;

};

template<typename T, typename Base>
inline void shared_future<T,Base>::resolve_cb::charge(std::shared_ptr<future_internal> ptr) {
       _ptr = ptr;
       if (!(ptr->operator co_await()).subscribe_awaiter(&ptr->resolve_tracer)) {
           _ptr = nullptr;
      }
}


}




#endif /* SRC_COCLS_SHARED_FUTURE_H_ */
