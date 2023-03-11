/*
 * async.h
 *
 *  Created on: 8. 3. 2023
 *      Author: ondra
 */

#ifndef SRC_COCLS_ASYNC_H_
#define SRC_COCLS_ASYNC_H_

#include "coro_queue.h"
#include "awaiter.h"

#include <cassert>
namespace cocls {

template<typename T> class future;
template<typename T> class promise;
template<typename T> class async_promise;



template<typename T>
class [[nodiscard]] async {
public:

    friend class future<T>;

    using promise_type = async_promise<T>;

    async(std::coroutine_handle<promise_type> h):_h(h) {}
    async(async &&other):_h(std::exchange(other._h, {})) {}

    ~async() {
        if (_h) _h.destroy();
    }

    ///Starts the coroutine by converting it to future. Allows to initialize resumption policy
    /**
     * The function is equivalent to converting coroutine to the future<T> directly. However
     * this function allows you to initialize resumption policy by passing arguments
     *
     * @return future object
     */
    future<T> start() {
        return future<T>(std::move(*this));
    }

    ///Starts the coroutine, registers promise, which is resolved once the coroutine finishes
    /**
     * This is useful, when you have a promise and you need to resolve it by this coroutine.
       @param p promise to resolve. The promise is claimed by this call
     * @retval true sucefully started, promise was claimed
     * @retval false failed to claim the promise, the coroutine remains suspended
     */
    bool start(cocls::promise<T> &p) {
        async_promise<T> &promise = _h.promise();
        promise._future = p.claim();
        if (promise._future) {
            start_coro();
            return true;
        }  else {
            return false;
        }
    }

    ///Detach coroutine
    /**
     * Allows to run coroutine detached. Coroutine is not connected
     * with any future variable, so result (and exception) is ignored
     */
    void detach() {
        start_coro();
    }

    ///Awaiter which allows to co_await the async<T> coroutine
    class co_awaiter: private awaiter, private future<T> {
    public:
        co_awaiter(std::coroutine_handle<> h) {
            this->set_handle(h);
        }
        bool await_ready() const noexcept {
            return this->_awaiter.load(std::memory_order_relaxed) == &awaiter::disabled;
        }
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            std::coroutine_handle<promise_type> start_handle = std::coroutine_handle<promise_type>::from_address(this->_handle_addr);
            auto &p = start_handle.promise();
            this->set_handle(h);
            this->_awaiter.store(this, std::memory_order_relaxed);
            p._future = this;
            return start_handle;
        }

        typename future<T>::reference await_resume() {
            return this->value();
        }
    };


    ///starts async coroutine, and co_awaits for result
    co_awaiter operator co_await() {
        return co_awaiter(std::exchange(_h,{}));
    }

    ///starts coroutine and blocks current thread until result is awailable. Then result is returned
    auto join() {
        return std::move(future<T>(*this).join());
    }


protected:
    void start_coro() {
        assert("There is no coroutine to start" && _h != nullptr);
        auto h = std::exchange(_h,{});
        coro_queue::resume(h);
    }



    std::coroutine_handle<promise_type> _h;
};

template<typename T, typename Derived>
class coro_unified_return {
public:
    template<typename X>
    CXX20_REQUIRES(std::constructible_from<T, X>)
    void return_value(X &&value) {
        static_cast<Derived *>(this)->resolve(std::forward<X>(value));
    }
};

template<typename Derived>
class coro_unified_return<void, Derived> {
public:
    void return_void() {
        static_cast<Derived *>(this)->resolve();
    }
};


template<typename T>
class async_promise: public coro_unified_return<T, async_promise<T> > {
public:
    future<T> *_future = nullptr;

    async<T> get_return_object() {
        return std::coroutine_handle<async_promise>::from_promise(*this);
    }
    struct final_awaiter: std::suspend_always {
        template<typename Prom>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<Prom> me) noexcept {
            //store noop coroutine handle for later usage
            std::coroutine_handle<> n = std::noop_coroutine();
            //retrieve promise object
            async_promise &p = me.promise();
            //retrieve future ponter, it can be nullptr for detached coroutine
            future<T> *f = p._future;
            //set future resolved - this must be done before frame is destroyed
            //as there can be still connection to the frame before resolution
            //once the future is resolved, there should be no connection at all.
            auto h = f ? f->resolve_resume():n;
            //now we can destroy our frame
            me.destroy();
            //return handle returned by resolve();
            return h;
        }
    };

    std::suspend_always initial_suspend() noexcept {return {};}
    final_awaiter final_suspend() noexcept {return {};}

    template<typename ... Args>
    void resolve(Args && ... args) {
        if (_future) _future->set(std::forward<Args>(args)...);
    }
    void unhandled_exception() {
        if (_future) _future->set(std::current_exception());
    }
};

}



#endif /* SRC_COCLS_ASYNC_H_ */
