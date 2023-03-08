/**
 * @file scheduler.h
 *
 * scheduler
 */
#ifndef SRC_COCLASSES_SCHEDULER_H_
#define SRC_COCLASSES_SCHEDULER_H_
#include "future.h"

#include "exceptions.h"
#include "callback_awaiter.h"
#include "thread_pool.h"

#include "generator.h"

#include "coro_queue.h"

#include <condition_variable>
#include <functional>
#include <optional>
#include <variant>
#include <vector>
#include <stop_token>


namespace cocls {

///Sheduler - schedule execution of coroutines. Exposes functions sleep_for and sleep_until for coroutines
/**
 * The scheduler can run in single thread application or can be started in one thread of
 * a thread pool. If started in single threaded application, it creates base for
 * manual scheduling and cooperative multitasking of coroutines. You can define
 * condition when the scheduler exits. When scheduler runs as thread of the thread pool,
 * it can benefit of having multiple threads. The scheduled coroutine is scheduled
 * on the current thread while the scheduler continues to run in the other thread. This
 * can allow to schedule coroutines in parallel.
 *
 * The scheduler works with promise<void> and future<void>. So the scheduling is not limited
 * to coroutines, you can actually schedule anything.
 *
 * Any scheduled task can be canceled. To identify task, you need to supply an identifier.
 *
 */
class scheduler {
public:

    ///Identifier of the task
    /** Identifier must be unique. To achieve this, identifier is stored as const void
     * pointer. This allows to easily make unique identifier. You can for example
     * point to any variable in current coroutine frame, its address is unique.
     */
    using ident = const void *;
    ///You can schedule promise, this defines exact type of that promise
    using promise = ::cocls::promise<void>;
    ///For manual scheduling, this type caries expired promise, or time of nearest event
    using expired = std::variant<std::chrono::system_clock::time_point, promise>;

    ///Construct inactive scheduler
    scheduler() = default;
    ///Construct scheduler and  immediately start it in a thread pool
    /**
     * @param pool reference to thread pool
     */
    scheduler(thread_pool &pool) {
        start_in(pool);
    }

    ///Construct scheduler and  immediately start it in a thread pool
    /**
     * @param pool reference to thread pool
     */
    scheduler(std::thread &thread) {
        start_in(thread);
    }


    ///Schedule a task using a promise
    /**
     * @param id identifier of task, can be nullptr if you not going to cancel it. Identifier
     * should be unique. In case of duplicates, any cancel request can choose randomly which
     * promise is canceled / it is canceled always one promise per one cancel request.
     * @param p promise to resolve
     * @param tp time point when resolve the promise. The time should be in the future. If
     * it is in the pass, the promise will be resolved as soon as possible, but in thread
     * of the scheduler (not here)
     *
     *
     */
    void schedule(ident id, promise p, std::chrono::system_clock::time_point tp) {
          std::lock_guard _(_mx);
          bool ntf = _scheduled.empty() || _scheduled[0]._tp > tp;
          _scheduled.push_back({tp, std::move(p), id});
          std::push_heap(_scheduled.begin(), _scheduled.end(), compare_item);
          if (ntf) {
              _cond.notify_all();
          }
      }

    ///Retrieves first expired promise or calculates time-point of first expiration
    /**
     * Useful for manual scheduling. If there is expired promise, it is removed and returned.
     * If there isn't such promise, returns time of first expired promise
     *
     * @param now you need to supply current time.
     * @return
     */
    expired get_expired(std::chrono::system_clock::time_point now) {
        std::lock_guard _(_mx);
        return get_expired_lk(now);
    }

    ///Remove scheduled promise referenced by identifier
    /**
     * @param id identifier of promise to remove. If there are more such promises,
     * it chooses randomly one.
     * @return removed promise. If there were no such promise, result is empty promise.
     * The promise can be used as expression in if() condition to detect, whether promise
     * is empty or not
     *
     * @code
     * promise p = remove(id);
     * if (p) {
     *     //promise removed and moved to p
     * }
     * @endcode
     */
    promise remove(ident id) {
        std::lock_guard _(_mx);
        if (_scheduled.empty()) return {};
        while (_scheduled[0]._ident == id) {
            auto p = std::move(_scheduled[0]._p);
            pop_item();
            if (p) return p;
        }
        SchVector::iterator iter = std::find_if(_scheduled.begin(), _scheduled.end(),[&](const SchItem &x) {
            return x._ident == id;
        });
        if (iter == _scheduled.end()) return {};
        return std::move(iter->_p);
    }

    ///sleeps until specified time-point is reached
    /**
     * Creates future, which resolves at given time-point. You can co_await this future to
     * suspend until the time is reached
     *
     * @param tp time point
     * @param id identifier which can be used to cancel the sleep
     * @return future, which resolves at given timepoint. The future throws exception
     * (default: await_canceled_exception) when wait is canceled
     *
     */
    future<void> sleep_until(std::chrono::system_clock::time_point tp, ident id = nullptr) {
        return [&](promise p) {
            schedule(id, std::move(p), tp);
        };
    }

    ///sleeps for specified duration
    /**
     * Creates future, which resolves after given duration. You can co_await this future to
     * suspend until the time is reached. The duration is counted from time when this function
     * has been called, not when the future started co_awaited
     *
     * @param dur duration
     * @param id identifier which can be used to cancel the sleep
     * @return future, which resolves after given duration. The future throws exception
     * (default: await_canceled_exception) when wait is canceled
     *
     */
    template<typename A, typename B>
    future<void> sleep_for(std::chrono::duration<A,B> dur, ident id = nullptr) {
        return sleep_until(std::chrono::system_clock::now()+dur, id);
    }

    ///cancel scheduled task (cancel sleep)
    /**
     * @param id identifier of task
     * @retval true canceled
     * @retval false not found
     *
     * @note associated future throws exception await_canceled_exception()
     *
     * @note associated promise is resolved in current thread, not in scheduler's thread
     */
    bool cancel(ident id) {
        return cancel(id, std::make_exception_ptr(await_canceled_exception()));
    }

    ///cancel scheduled task (cancel sleep), you can specify own exception
    /**
     * @param id identifier of task
     * @param e exception which will be thrown
     * @retval true canceled
     * @retval false not found
     *
     * @note associated promise is resolved in current thread, not in scheduler's thread
     */
    bool cancel(ident id, std::exception_ptr e) {
        auto p = remove(id);
        if (p) {
            if (_glob_state.has_value() && _glob_state->_pool) {
                _glob_state->_pool->resolve(p, e);
            } else {
                p(e);
            }
            return true;
        } else {
            return false;
        }
    }

    ///Starts the scheduler in current thread
    /**
     * Starts scheduler in current thread. The scheduler block execution of current thread
     * and starts to schedule coroutines.
     *
     * @param awt function simulates co_await. You can pass anything which can be co_await-ed.
     * Once the awaiter is resolved, the scheduler stops and function returns value
     *  of the await operation.
     *
     * @return value of await operation
     *
     * @note it is possible to start scheduler in multiple threads. You can also start
     * scheduler recursively. Each thread can access to shared list of scheduled tasks.
     *
     *
     * @note If you pass thread_pool to this function, the scheduler starts in the thread_pool
     * without blocking current thread. In this case, it can run only once.
     *
     * @note If you pass std::thread to this function, the scheduler starts in
     * that thread without blocking current thread. In this case, it can run only once.
     */
    template<typename Awt>
    auto start(Awt &&awt) {

        if constexpr(std::is_convertible_v<Awt, thread_pool &>) {
            start_in(awt);
        } else if constexpr(std::is_convertible_v<Awt, std::thread &>) {
            start_in(awt);
        } else {

            std::exception_ptr e;
            std::stop_source stps;
            using AwtRetVal = std::decay_t<awaiter_return_value<Awt> >;
            auto worker = worker_coro(stps.get_token());

            if constexpr(std::is_void_v<AwtRetVal>) {

                auto fn = [&](auto ... x){
                    if constexpr(sizeof...(x) == 1) {
                        e = std::exception_ptr(x...);
                    }
                    stps.request_stop();
                };

                auto wt = callback_await<Awt &>(awt, fn);

                coro_queue::install_queue_and_call([&]{
                    worker.detach();
                });
                if (e) std::rethrow_exception(e);
            } else {
                std::optional<AwtRetVal> ret;

                auto wt = callback_await<Awt &>(awt, [&](auto x){
                    if constexpr(std::is_same_v<decltype(x), std::exception_ptr>) {
                        e = std::exception_ptr(x);
                    } else {
                        ret.emplace(std::move(x));
                    }
                    stps.request_stop();
                });
                coro_queue::install_queue_and_resume([&]{
                    worker.detach();
                });
                if (e) std::rethrow_exception(e);
                return *ret;
            }
        }
    }

    void start_thread() {
        std::thread thr;
        start_in(thr);
        thr.detach();
    }

    ///Creates generator of intervals
    /**
     * @param dur duration of interval.
     * @param token stop token which can be used to stop genertion
     * @return generator
     *
     * the generator can be called which returns future. This future is resolved after
     * given interval. Then generator is stopped until it is called again. The interval calculation
     * is made before co_yield, so if the processing of the tick is shorter then tick
     * itself, you can achieve precise ticking regardless on how long you process each tick.
     *
     * The generator must be destroyed by owner when the generator is paused on co_yield.
     * If you need to stop it while its waiting on interval, you can use stop token. Activating
     * stop token causes that generator finishes generation as soon as possible.
     *
     */
    template<typename A, typename B>
    generator<std::size_t> interval(std::chrono::duration<A,B> dur, std::stop_token token = {}) {
        bool tag;
        std::stop_callback stpc(token,[&]{
            std::lock_guard _(_mx);
            this->cancel(&tag);
        });
        std::size_t counter;
        future<void> waiter;
        std::chrono::system_clock::time_point next = std::chrono::system_clock::now()+dur;
        try {
            while (!token.stop_requested()) {
                waiter << [&]{return this->sleep_until(next, &tag);};
                co_await waiter;
                next = std::chrono::system_clock::now()+dur;
                co_yield counter;
                ++counter;
            }

        } catch (const await_canceled_exception &) {
            //empty
        }
    }


    ~scheduler() {
        if (_glob_state.has_value()) {
            _glob_state->_stp.request_stop();
            _glob_state->_fut.wait();
        }
    }

protected:

    struct SchItem { // @suppress("Miss copy constructor or assignment operator")
        std::chrono::system_clock::time_point _tp;
        promise _p;
        ident _ident = nullptr;

    };

    struct GlobState {
        GlobState() {};
        future<void> _fut;
        std::stop_source _stp;
        thread_pool *_pool = nullptr; //active thread pool, nullptr if not
    };

    using SchVector = std::vector<SchItem>;
    SchVector _scheduled;
    std::mutex _mx;
    std::condition_variable _cond;
    std::optional<GlobState> _glob_state;


    static bool compare_item(const SchItem &a, const SchItem &b) {
        return a._tp > b._tp;
    }

    void pop_item() {
        std::pop_heap(_scheduled.begin(), _scheduled.end(), compare_item);
        _scheduled.pop_back();
    }

    async<void> worker_coro(std::stop_token state) {
        std::stop_callback stop_notify(state, [&]{
            _cond.notify_all();
        });
        std::unique_lock lk(_mx);
        std::chrono::system_clock::time_point now;
        thread_pool *pool = _glob_state.has_value()?_glob_state->_pool:nullptr;
        while (!state.stop_requested()) {
            lk.unlock();
            co_await pause();
            lk.lock();
            if (state.stop_requested()) break;
            now = std::chrono::system_clock::now();
            expired p = get_expired_lk(now);
            std::visit([&](auto &x){
               using T = std::decay_t<decltype(x)>;
               if constexpr(std::is_same_v<T, promise>) {
                   if (pool) pool->resolve(x); else x(); //resolve if pool defined, use pool
               } else {
                   if (coro_queue::can_block() && (!pool || pool->any_enqueued())) {
                       _cond.wait_until(lk, x);
                   }
               }
            }, p);
        }
    }

    expired get_expired_lk(std::chrono::system_clock::time_point now) {
        while (!_scheduled.empty() && (_scheduled[0]._tp <= now || !_scheduled[0]._p)) {
            auto p = std::move(_scheduled[0]._p);
            pop_item();
            if (p) return std::move(p);
        }
        if (_scheduled.empty()) return std::chrono::system_clock::time_point::max();
        else return _scheduled[0]._tp;
    }



    void start_in(thread_pool &pool) {
        assert("Scheduler already started" && !_glob_state.has_value());
        if (_glob_state.has_value()) return;
        _glob_state.emplace();
        _glob_state->_pool = &pool;
        _glob_state->_fut << [&]{
            return pool.run(worker_coro(_glob_state->_stp.get_token()));
        };
    }

    void start_in(std::thread &thr) {
        assert("Scheduler already started" && !_glob_state.has_value());
        if (_glob_state.has_value()) return;
        _glob_state.emplace();
        auto promise = _glob_state->_fut.get_promise();
        thr = std::thread([this,promise = std::move(promise)]() mutable {
            worker_coro(_glob_state->_stp.get_token()).start_set_promise(promise);
        });
    }
};





}



#endif /* SRC_COCLASSES_SCHEDULER_H_ */
