/**
 * @file thread_pool.h
 *
 *  Created on: 6. 10. 2022
 *      Author: ondra
 */
#pragma once
#ifndef SRC_cocls_THREAD_POOL_H_
#define SRC_cocls_THREAD_POOL_H_
#include "common.h"
#include "future.h"
#include "exceptions.h"
#include "generics.h"

#include "function.h"

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <variant>
#include <vector>


namespace cocls {
///thread pool for coroutines.
/** Main benefit of such object is zero allocation during transferring the coroutine to the
 * other thread
 *
 */

class thread_pool {
public:

    using q_item = function<void()>;

    ///Start thread pool
    /**
     * @param threads count of threads. Default value creates same amount as count
     * of available CPU cores (hardware_concurrency)
     */
    thread_pool(unsigned int threads = 0)
    {
        if (!threads) threads = std::thread::hardware_concurrency();
        for (unsigned int i = 0; i < threads; i++) {
            _threads.push_back(std::thread([this]{worker();}));
        }
    }


    ///Start a worker
    /**
     * By default, workers are started during construction. This function allows
     * to add a worker. Current thread becomes a worker until stop() is called.
     */
    void worker() {
        _current = this;
        std::unique_lock lk(_mx);
        for(;;) {
            _cond.wait(lk, [&]{return !_queue.empty() || _exit;});
            if (_exit) break;
            auto h = std::move(_queue.front());
            _queue.pop();
            lk.unlock();
            h();
            //if _current is nullptr, thread_pool has been destroyed
            if (_current == nullptr) return;
            lk.lock();
        }
    }

    ///Stops all threads
    /**
     * Stopped threads cannot be restarted
     */
    void stop() {
        decltype(_threads) tmp;
        decltype(_queue) q;
        {
            std::unique_lock lk(_mx);
            _exit = true;
            _cond.notify_all();
            std::swap(tmp, _threads);
            std::swap(q, _queue);
        }
        auto me = std::this_thread::get_id();
        for (std::thread &t: tmp) {
            if (t.get_id() == me) {
                t.detach();
                //mark this thread as ordinary thread
                _current = nullptr;
            }
            else {
                t.join();
            }
        }
    }

    ///Destroy the thread pool
    /**
     * It also stops all threads
     */
    ~thread_pool() {
        stop();
    }


    class co_awaiter {
    public:
        co_awaiter() = default;
        co_awaiter(thread_pool &owner):_owner(&owner) {}
        co_awaiter(const co_awaiter&) = default;
        co_awaiter &operator=(const co_awaiter&) = delete;

        static constexpr bool await_ready() {return false;}

        void await_suspend(std::coroutine_handle<> h) {

            //this lambda function is called when enqueued function is destroyed
            auto fin = [](co_awaiter *x) {
                //resume coroutine (in queue if possible)
                //we will throw exception when await_resume()
                coro_queue::resume(x->_h);
            };

            this->_h = h;

            //enqueue function
            //awtptr contains pointer to our awaiter
           _owner->enqueue([awtptr = std::unique_ptr<co_awaiter, decltype(fin)>(this,fin)]() mutable {
               //when function is called
               //retrieve handle
               auto h = awtptr->_h;
               //reset our handle - so the deleter will see that we successed
               awtptr->_h = nullptr;
               //release pointer, as we don't need to call the deleter
               awtptr.release();

               coro_queue::resume(h);
           });
        }

        void await_resume() {
            //when handle is still set, we did not called the function
            //and we was resumed from deleter
            //so throw exception
            if (_h) throw await_canceled_exception();
        }

    protected:
        thread_pool *_owner = nullptr;
        std::coroutine_handle<> _h;
    };

    template<typename Awt>
    class enqueue_awaiter: public awaiter {
    public:
        enqueue_awaiter(Awt &&awt, thread_pool &pool):_awt(std::forward<Awt>(awt)),_pool(pool) {}
        constexpr bool await_ready() {return _awt.await_ready();}
        constexpr auto await_suspend(std::coroutine_handle<> h) {
            set_handle(h);
            return _awt.await_suspend(&perform_resume, this);
        }
        constexpr decltype(auto) await_resume() {return _awt.await_resume();}
    protected:

        static void perform_resume(awaiter *, void *user_ptr, std::coroutine_handle<> &) noexcept {
            enqueue_awaiter *_this = reinterpret_cast<enqueue_awaiter *>(user_ptr);
            _this->_pool.run_detached([_this]{
                _this->resume();
            });
        }
        Awt _awt;
        thread_pool &_pool;
    };


    /// converts awaitable/awaiter to resume awaiting coroutine in thread pool
    /**
     * Example to use this operator
     * @code
     * //normal usage
     * int ret = co_await calc_async();
     *
     * //thread pool
     * thread_pool pool;
     * int ret2 = co_await pool(calc_async());
     * @endcode
     *
     * Operator converts awaitable to awaiter which is able to resume coroutine with allocating a thread in the thread
     * pool. So coroutine will not block the thread which caused resumption and also is resumed immediately, and it
     * is not put to coroutine queue (instead it is put to thread_pool queue)
     *
     * If the awaiting operation is already resolved, no thread is allocated and execution continues in current thread
     *
    */
    template<typename Awaitable>
    auto operator()(Awaitable &&awt) -> enqueue_awaiter<decltype(retrieve_awaiter(std::declval<Awaitable>()))> {
        using AWT = decltype(retrieve_awaiter(std::forward<Awaitable>(awt)));
        return enqueue_awaiter<AWT>(std::forward<Awaitable>(awt), *this);
    }


    ///Transfer coroutine to the thread pool
    /**
     *
     * @return awaiter
     *
     * @code
     * task<> coro_test(thread_pool &p) {
     *      co_await p;
     *      //now we are running in the thread pool
     * }
     * @endcode
     */
    co_awaiter operator co_await() {
        return *this;
    }

    ///Run function in thread pool
    /**
     *
     * @param fn function to run. The function must return void. The function
     * run() returns immediately
     */
    template<typename Fn>
    CXX20_REQUIRES(std::same_as<void, decltype(std::declval<Fn>()())>)
    void run_detached(Fn &&fn) {
        enqueue(q_item(std::forward<Fn>(fn)));
    }

    ///Runs function in thread pool, returns future
    /**
     * Works similar as std::async. It just runs function in thread pool and returns
     * cocls::future.
     * @param fn function to run
     * @return future<Ret> where Ret is return value of the function
     */
    template<typename Fn>
    auto run(Fn &&fn) -> future<decltype(std::declval<Fn>()())> {
        using RetVal = decltype(std::declval<Fn>()());
        return [&](auto promise) {
            run_detached([fn = std::tuple<Fn>(std::forward<Fn>(fn)), promise = std::move(promise)]() mutable {
                try {
                    if constexpr(std::is_void_v<RetVal>) {
                        std::get<0>(fn)();
                        promise();
                    } else {
                        promise(std::get<0>(fn)());
                    }
                } catch(...) {
                    promise(std::current_exception());
                }
            });
        };
    }
    ///Resolve promise in thread
    /** Allocates thread and resolves promise in it. If the
     * associated coroutine is resumed, it is resumed in this allocated thread
     * @param t promise
     * @param args arguments
     *
     * @note if the promise t is not valid, no thread is allocated
     */
    template<typename T, typename ... Args>
    void resolve(promise<T> &t, Args && ... args) {
        if (t) run_detached([p = t.bind(std::forward<Args>(args)...)]() mutable{
            p();
        });
    }


    ///Run coroutine async
    /**
     * @param coro coroutine to run
     * @return future which resolves when coroutine ends
     */
    template<typename T>
    future<T> run(async<T> &&coro) {
        return [&](auto promise) {
            enqueue([promise = std::move(promise), coro = std::move(coro)]()mutable{
                coro.start(promise);
            });
        };
    }

    ///Runs coroutine async, discard future
    /**
     * @param coro coroutine to run
     * @param args optional arguments passed to resumption policy.
     */
    template<typename T>
    void run_detached(async<T> &&coro) {
        enqueue([coro = std::move(coro)]() mutable {
           coro.detach();
        });
    }

    ///Resumes prepared coroutine in a thread
    /**
     * By passing a suspend point to this function, you can resume the coroutine, which is ready
     * to run, in different thread. So the coroutine will continue in thread allocated in this thread pool
     * while the current code will continue to run without interruption.
     *
     * @param spt suspend point
     * @return a value associated with suspend point - if any
     *
     * @note The function supports a suspend point which acts as awaiter and it is able to return
     * a coroutine handle from the await_suspend(); The returned handle is passed to the thread pool
     * and resumed there. The function calls await_ready, await_suspend and await_resume in the correct
     * order.
     */
    template<typename T, typename P>
    auto resume(suspend_point<T, P> &&spt) {
        if (!spt.await_ready()) {
            auto n = std::noop_coroutine();
            std::coroutine_handle<> h = spt.await_suspend(n);
            if (h != n) {
                enqueue([h]{
                    coro_queue::resume(h);
                });
            }
        }
        return spt.await_resume();

    }


    struct current {

        class  current_awaiter: public co_awaiter {
        public:
            current_awaiter():co_awaiter(*_current) {}
            static bool await_ready() {
                thread_pool *c = _current;
                return c == nullptr || c->_exit;
            }
        };

        current_awaiter operator co_await() {
            return current_awaiter();
        }

        static bool is_stopped() {
            thread_pool *c = _current;
            return !c || c->is_stopped();
        }

        ///returns true if there is still enqueued task
        static bool any_enqueued() {
            thread_pool *c = _current;
            if (c) return c->any_enqueued();
            return false;

        }

    };

    bool is_stopped() const {
        std::lock_guard _(_mx);
        return _exit;
    }


    ///returns true if there is still enqueued task
    bool any_enqueued() {
        std::unique_lock lk(_mx);
        return _exit || !_queue.empty();
    }

    friend bool is_current(const thread_pool &pool) {
        return _current == &pool;
    }





protected:


    void enqueue(q_item &&fn) {
        std::lock_guard _(_mx);
        if (!_exit) {
            _queue.push(std::move(fn));
            _cond.notify_one();
        }
    }


    mutable std::mutex _mx;
    std::condition_variable _cond;
    std::queue<q_item> _queue;
    std::vector<std::thread> _threads;
    bool _exit = false;
    static thread_local thread_pool *_current;




};

 inline thread_local thread_pool *thread_pool::_current;

}



#endif /* SRC_cocls_THREAD_POOL_H_ */
