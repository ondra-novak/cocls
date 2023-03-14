/** @file awaiter.h */

#pragma once
#ifndef SRC_cocls_AWAITER_H_
#define SRC_cocls_AWAITER_H_

#include "coro_queue.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>

namespace cocls {

class awaiter;

///collects awaiters ready to be notified
/**
 * the collector is single atomic pointer, which allows to implement
 * lockfree stack. Every awaiter has member variable _next which points to
 * next awaiter in the collector.
 */
using awaiter_collector = std::atomic<awaiter *>;


///Helps to store coroutine handle to be resumed.
class awaiter {
public:

    /// Defines function prototype which is responsible to resume awaiting object which is not coroutine
    /**
     *  The function is statically defined to maintain minimum requierement for basic awaitar.
     *  @param _this pointer to associated awaiter object. You need to static_cast<deriver> to
     *          retrieve pointer to derived instance.
     *  @param _context arbitrary pointer which meaning is defined by the owner.
     *  @param _out_handle reference to handle which can be modified by the called function. By chainging
     *      the value of the handle is interpreted that specified coroutine must be resumed, so
     *      the function just prepared resumption. By leaving value not modified is interprted
     *      that resume function finished resumption, no coroutine is resumed.
     *
     *
     */
    using resume_fn = void (*)(awaiter *_this, void *_context, std::coroutine_handle<> &_out_handle) noexcept;

    awaiter() = default;

    /// Resumes object associated with this awaiter
    /**
     *  If the object is coroutine, then coroutine is resumed or scheduled for resumption
     *  in the resumption queue. If the awaiter is not associated with coroutine, but with
     *  other object - for example with a callback - the associated operation is perfomed (a callback
     *  is called for instance)
     *
     *  @note Function is declared as noexcept. Any callback function must avoid to throw exception
     */
    void resume() noexcept {
        if (_resume_fn) {
            std::coroutine_handle<> h;
            _resume_fn(this, _handle_addr, h);
            if (h) coro_queue::resume(h);
        }
        else {
            coro_queue::resume(std::coroutine_handle<>::from_address(_handle_addr));
        }
    }

    /// Repares the awaiter for resumption and returns coroutine handle to be resumed
    /**
     * THis function is called during await_suspend() when a coroutine is being suspended. The
     * function returns handle of coroutine to resume. This helps to transfer execution by a
     * symmetric transfer (without adding a stack level), which is much faster than classic
     * transfer.
     *
     * If the awaiter is not associated with a coroutine, its function is called directly and
     * then await_suspend() is left without switching to a coroutine
     **/
    std::coroutine_handle<> resume_handle() noexcept {
        if (_resume_fn) {
            std::coroutine_handle<> h;
            _resume_fn(this, _handle_addr, h);
            if (h) { //resume returned coroutine
                return h;
            } else {
                //this coroutine is being suspended, so retrieve next queued coroutine and resume it
                return coro_queue::resume_handle_next();
            }
        } else {
            return std::coroutine_handle<>::from_address(_handle_addr);
        }
    }

    void subscribe(awaiter_collector &chain) {
        assert (this != chain.load(std::memory_order_relaxed));
        //release memory order because we need to other thread to see change of _next
        //this is last operation of this thread with awaiter
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_release));

        assert (_next != this);
    }
    ///releases chain atomicaly
    /**
     * @param chain holds chain
     * @return count of released awaiters (including skipped)
     */
    static std::size_t resume_chain(awaiter_collector &chain) {
        //acquire memory order, we need to see modifications made by other thread during registration
        //this is first operation of the thread of awaiters
        return resume_chain_lk(chain.exchange(nullptr, std::memory_order_acquire));
    }

    ///Resume chain and marks its ready
    /**
     * @param chain chain to resume
     * @param ready_state state in meaning ready
     * @return count of awaiters
     *
     * @note It marks chain disabled, so futher registration are rejected with false
     * @see subscribe_check_ready()
     */
    static std::size_t resume_chain_set_ready(awaiter_collector &chain, awaiter &ready_state) {
        //acquire memory order, we need to see modifications made by other thread during registration
        //this is first operation of the thread of awaiters
        return resume_chain_lk(chain.exchange(&ready_state, std::memory_order_acquire));
    }
    static std::size_t resume_chain_lk(awaiter *chain) {
        std::size_t n = 0;
        while (chain) {
            auto y = chain;
            chain = chain->_next;
            y->_next = nullptr;
            y->resume();
            n++;
        }
        return n;
    }
    ///subscribe this awaiter but at the same time, check, whether it is marked ready
    /**
     * @note uses awaiter<true>::disabled to mark whether the value is ready.
     *
     * @param chain register to chain
     * @retval true registered
     * @retval false registration unsuccessful, the object is already prepared
     */
    bool subscribe_check_ready(awaiter_collector &chain, awaiter &ready_state) {
        assert(this->_next == nullptr);
        //release mode - because _next can change between tries
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_release)) {
            if (_next == &ready_state) {
                _next = nullptr;
                //empty load, but enforce memory order acquire because this thread will
                //access to result
                std::atomic_thread_fence(std::memory_order_acquire);
                return false;
            }
        }
        return true;
    }

    awaiter *_next = nullptr;

    ///Non-null instance containing empty function
    static awaiter instance;
    ///Non null instance serves as disabled awaiter slot;
    static awaiter disabled;

protected:

    void set_handle(std::coroutine_handle<> h) {
        _resume_fn = nullptr;
        _handle_addr = h.address();
    }

    void set_resume_fn(resume_fn fn, void *user_ptr = nullptr) {
        _resume_fn = fn;
        _handle_addr = user_ptr;
    }



    void *_handle_addr = nullptr;
    resume_fn _resume_fn = &null_fn;
    ///This value is set to true by resume() or resume_handle() when a coroutine handle is resumed
    /**
     * Because recursive resume is not possible,
     */
    bool _will_be_resumed = false;

    static void null_fn(awaiter *, void *, std::coroutine_handle<> &) noexcept {}



};

inline awaiter awaiter::instance;
inline awaiter awaiter::disabled;

///Generic awaiter used in most object to handle co_await
template<typename promise_type>
class [[nodiscard]] co_awaiter: public awaiter {
public:
    co_awaiter(promise_type &owner):_owner(owner) {}
    co_awaiter(const co_awaiter &) = default;
    co_awaiter &operator=(const co_awaiter &) = delete;


    ///co_await related function
    bool await_ready() {
        return this->_owner.ready();
    }
    ///co_await related function
    bool await_suspend(std::coroutine_handle<> h) {
        set_handle(h);
        return this->_owner.subscribe_awaiter(this);
    }
    ///suspend coroutine but register function to be resumed instead of coroutine itself
    bool await_suspend(resume_fn fn, void *user_ctx) {
        set_resume_fn(fn,user_ctx);
        return this->_owner.subscribe_awaiter(this);
    }
    ///co_await related function
    decltype(auto) await_resume(){
        return this->_owner.value();
    }

    ///Wait synchronously
    /**
     * Blocks execution until awaiter is signaled
     * @return result of awaited operation
     */
    decltype(auto) wait();

    ///Wait synchronously
    /**
     * Blocks execution until awaiter is signaled
     * Doesn't pick neither result nor exception
     * Useful if you need to synchronize with awaiter without being
     * affected by the result - for example without need to handle exception
     */
    void sync() noexcept ;


    ///Subscribe custom awaiter
    /**
     * Allows to declare custom awaiter, which is resumed, when awaited result is ready.
     * @param awt reference to awaiter. You need to keep reference valid until it is called
     * @retval true registration done
     * @retval false awaiting expression is already resolved, so no registration done, you can
     * call await_resume()
     */
    bool subscribe_awaiter(awaiter *awt) {
        return this->_owner.subscribe_awaiter(awt);
    }




protected:
    promise_type &_owner;
};

///Malleable awaiter is awaiter, which has functions set_resume_fn and set_handle set public
/**
 * This allows to change behavior of the awaiter in runtime without introduction
 * of new subclasses
 */
class malleable_awaiter : public awaiter {
public:
    using awaiter::set_resume_fn;
    using awaiter::set_handle;
};


class sync_awaiter: public awaiter {
public:
    std::atomic<bool> flag = {false};
    sync_awaiter() {
        set_resume_fn(&sync_awaiter::wakeup);
    }
    sync_awaiter(const sync_awaiter &) = delete;
    sync_awaiter &operator=(const sync_awaiter &) = delete;

    void wait_sync() {
        flag.wait(false);
    }

    void wakeup() {
        flag.store(true);
        flag.notify_all();
    }

    static void wakeup(awaiter *me, void *, std::coroutine_handle<> &) noexcept {
        static_cast<sync_awaiter *>(me)->wakeup();
    }
};



template<typename promise_type>
inline decltype(auto) co_awaiter<promise_type>::wait() {
    sync();
    return await_resume();
}


template<typename promise_type>
inline void co_awaiter<promise_type>::sync() noexcept  {
    if (await_ready()) return ;
    sync_awaiter awt;
    if (subscribe_awaiter(&awt)) {
        awt.flag.wait(false);
    }
}

///Suspend point - place where would be good idea to co_await result
/**
 * Result of this type is optionally co_awaited, which can
 * bring some benefits. Typically by co_awaiting the
 * result helps to associated coroutine to be executed earlier.
 *
 * You can "switch to" the associated coroutine.
 *
 * However you can also ignore return value or retrieve just
 * actual result without co_awaiting, which performs
 * default action, which is also pefromed in non-coroutine world
 *
 *
 * @tparam RetVal return value - type of value wrapped into
 * suspend point to be retrieved
 * @tparam Impl class which implements this suspend_point. Each
 * class can use different implementation and support different
 * return values.
 *
 * This class is incomplette. Each implementation comes as
 * specifalization.
 *
 *
 */
template<typename RetVal, typename Impl>
class suspend_point;


}
#endif /* SRC_cocls_AWAITER_H_ */

