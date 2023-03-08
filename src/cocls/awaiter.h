/** @file awaiter.h */

#pragma once
#ifndef SRC_COCLASSES_AWAITER_H_
#define SRC_COCLASSES_AWAITER_H_

#include "coro_queue.h"


#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>

namespace cocls {



///Abstract awaiter interface
class iawaiter_t {
public:

    ///called to resume coroutine
    /**When called, the coroutine must be scheduled for execution, immediately or by rules of the resumption policy */
    virtual void resume() noexcept = 0;
    ///called to retrieve coroutine handle for symmetric transfer
    /**When called, function must prepare handle of coroutine to be executed on current thread. When
     * the awaiter doesn't resume a coroutine, it can do anything what it want and then return
     * std::noop_coroutine. In most of cases, it call resume()
     */
    virtual std::coroutine_handle<> resume_handle() noexcept = 0;
    virtual ~iawaiter_t() = default;

};

///Abstract awaiter - extentends interface
/**
 * Can be chained - public variable *_next - NOTE: content of variable is ignored during destruction,
 * but it is used to chain awaiters to make waiting lists
 */
class abstract_awaiter: public iawaiter_t {
public:
    abstract_awaiter() = default;
    abstract_awaiter(const abstract_awaiter &)=default;
    abstract_awaiter &operator=(const abstract_awaiter &)=delete;

    void subscribe(std::atomic<abstract_awaiter *> &chain) {
        assert (this != chain.load(std::memory_order_relaxed));
        //release memory order because we need to other thread to see change of _next
        //this is last operation of this thread with awaiter
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_release));

        assert (_next != this);
    }
    ///releases chain atomicaly
    /**
     * @param chain holds chain
     * @param skip awaiter to be skipped
     * @return count of released awaiters (including skipped)
     */
    static std::size_t resume_chain(std::atomic<abstract_awaiter *> &chain, abstract_awaiter *skip) {
        //acquire memory order, we need to see modifications made by other thread during registration
        //this is first operation of the thread of awaiters
        return resume_chain_lk(chain.exchange(nullptr, std::memory_order_acquire), skip);
    }

    ///Resume chain and marks its ready
    /**
     * @param chain chain to resume
     * @param ready_state state in meaning ready
     * @param skip awaiter to be skipped, can be nullptr
     * @return count of awaiters
     *
     * @note It marks chain disabled, so futher registration are rejected with false
     * @see subscribe_check_ready()
     */
    static std::size_t resume_chain_set_ready(std::atomic<abstract_awaiter *> &chain, abstract_awaiter &ready_state, abstract_awaiter *skip) {
        //acquire memory order, we need to see modifications made by other thread during registration
        //this is first operation of the thread of awaiters
        return resume_chain_lk(chain.exchange(&ready_state, std::memory_order_acquire), skip);
    }
    static std::size_t resume_chain_lk(abstract_awaiter *chain, abstract_awaiter *skip) {
        std::size_t n = 0;
        while (chain) {
            auto y = chain;
            chain = chain->_next;
            y->_next = nullptr;
            if (y != skip) y->resume();
            n++;
        }
        return n;
    }
    ///subscribe this awaiter but at the same time, check, whether it is marked ready
    /**
     * @note uses empty_awaiter<true>::disabled to mark whether the value is ready.
     *
     * @param chain register to chain
     * @retval true registered
     * @retval false registration unsuccessful, the object is already prepared
     */
    bool subscribe_check_ready(std::atomic<abstract_awaiter *> &chain, abstract_awaiter &ready_state) {
        assert(this->_next == nullptr);
        //release mode - because _next can change between tries
        while (!chain.compare_exchange_weak(_next, this, std::memory_order_release)) {
            if (_next == &ready_state) {
                _next = nullptr;
                //empty load, but enforce memory order acquire because this thread will
                //access to result
                chain.load(std::memory_order_acquire);
                return false;
            }
        }
        return true;

    }

    virtual std::coroutine_handle<> resume_handle() noexcept override {
        resume();
        return std::noop_coroutine();
    }

    abstract_awaiter *_next = nullptr;
};




///phony awaiter it is used to signal special value in awaiter's/chain
/** This awaiter doesn't resume anything */
class empty_awaiter: public abstract_awaiter {
public:

    ///Just instance for any usage
    static empty_awaiter instance;
    ///Disables awaiter's chain/slot. Any further registrations are impossible
    /** This allows to atomically replace awaiter with disabled, which can be
     * interpreted as "value is ready, no further waiting is required" while current
     * list of awaiters is picked and the awaiters are resumed
     *
     * @see abstract_awaiter::resume_chain_set_disabled
     */
    static empty_awaiter disabled;

    virtual void resume() noexcept override {}
    virtual std::coroutine_handle<> resume_handle() noexcept override {return std::noop_coroutine();}



};

inline empty_awaiter empty_awaiter::instance;
inline empty_awaiter empty_awaiter::disabled;

///Generic awaiter used in most object to handle co_await
template<typename promise_type>
class [[nodiscard]] co_awaiter: public abstract_awaiter {
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
        this->_h = h;
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
    bool subscribe_awaiter(abstract_awaiter *awt) {
        return this->_owner.subscribe_awaiter(awt);
    }

    operator decltype(auto) () {
        return wait();
    }




protected:
    promise_type &_owner;
    std::coroutine_handle<> _h;

    virtual void resume() noexcept {
        coro_queue::resume(_h);
    }
    virtual std::coroutine_handle<> resume_handle() noexcept {
        return _h;
    }

};

class sync_awaiter: public abstract_awaiter {
public:
    std::atomic<bool> flag = {false};

    virtual std::coroutine_handle<> resume_handle() noexcept override {
        sync_awaiter::resume();
        return std::noop_coroutine();
    }
    virtual void resume() noexcept override {
        flag.store(true);
        flag.notify_all();
    }
    void wait_sync() {
        flag.wait(false);
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
#endif /* SRC_COCLASSES_AWAITER_H_ */

