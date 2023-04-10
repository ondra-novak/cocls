/** @file awaiter.h */

#pragma once
#ifndef SRC_cocls_AWAITER_H_
#define SRC_cocls_AWAITER_H_

#include "coro_queue.h"
#include "suspend_point.h"

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
     *  @return Suspend point, if the function set coroutines ready, it can put them to the returned suspend point
     *
     *
     */
    using resume_fn = suspend_point<void> (*)(awaiter *_this, void *_context) noexcept;

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
    suspend_point<void> resume() noexcept {
        if (_resume_fn) {
            return _resume_fn(this, _handle_addr);
        }
        else {
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
    static suspend_point<void> resume_chain(awaiter_collector &chain) {
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
    static suspend_point<void> resume_chain_set_ready(awaiter_collector &chain, awaiter &ready_state) {
        //acquire memory order, we need to see modifications made by other thread during registration
        //this is first operation of the thread of awaiters
        return resume_chain_lk(chain.exchange(&ready_state, std::memory_order_acquire));
    }
    static suspend_point<void> resume_chain_lk(awaiter *chain) {
        suspend_point<void> ret;
        while (chain) {
            auto y = chain;
            chain = chain->_next;
            y->_next = nullptr;
            ret << y->resume();
        }
        return ret;
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

    static suspend_point<void> null_fn(awaiter *, void *) noexcept {return {};}



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

    static suspend_point<void> wakeup(awaiter *me, void *) noexcept {
        static_cast<sync_awaiter *>(me)->wakeup();
        return {};
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



}
#endif /* SRC_cocls_AWAITER_H_ */

