#pragma once
#ifndef SRC_COCLS_CORO_QUEUE_H_
#define SRC_COCLS_CORO_QUEUE_H_


#include "common.h"

#include <cassert>
#include <queue>
#include <utility>

namespace cocls {



///Trailer is object, which executes function at the end of block
/**
 * Trailer is called even after return.
 * @tparam Fn function to call
 */
template<typename Fn>
class trailer {
public:
    trailer(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
    ~trailer() noexcept(false) {
        _fn();
    }
protected:
    Fn _fn;
};

template<typename Fn>
trailer(Fn) -> trailer<Fn>;


///Coroutines are scheduled using queue which is managed in current thread
/**
 * The queue is initialized when the first coroutine is called and the function
 * doesn't return until the queue is empty. The queue is active only if there are
 * running coroutines. When the last coroutine finishes, the queue is destroyed (and
 * can be reinstalled again)
 *
 * Coroutines are started immediately (no suspension at the beginning), however the first
 * coroutine performs temporary suspend needed to initialize the queue. Further coroutines
 * are started with no temporary suspension
 *
 * When coroutine is being resumed, its handle is placed to the queue and it is resumed once
 * the current coroutine is finished or suspended.
 *
 * When a coroutine is finished, it performs symmetric transfer to the coroutine which is
 * waiting on result. This literally skips the queue in favor awaiting coroutine.
 */
struct coro_queue {

    struct queue_impl {

        queue_impl() = default;
        queue_impl(const coro_queue &) = delete;
        queue_impl&operator=(const coro_queue &) = delete;
        std::queue<std::coroutine_handle<> > _queue;

        void flush_queue() noexcept {
            while (!_queue.empty()) {
                auto h = std::move(_queue.front());
                _queue.pop();
                h.resume();
            }

        }

        static thread_local queue_impl instance;
    };


    static bool is_active() {
        return instance != nullptr;
    }

    ///Installs coroutine queue and calls the function
    /**
     * This is useful,if you need to ensure, that coroutines - that
     * with resumption policy: queued, are not resumed immediately, because
     * no queue has been installed yet. The function installs the queue and calls
     * the function. Extra arguments are passed to the function. Return value of
     * the function is returned. Queue is uninstalled upon return resuming all
     * queued coroutines at this point.
     *
     * @note if there is already active queue, a new nested queue is installed and
     * scheduled coroutines are placed here. They are resumed and processed before
     * the function returns.
     *
     * @param fn function to call
     * @param args arguments passed to function
     * @return
     */
    template<typename Fn, typename ... Args>
    CXX20_REQUIRES(std::invocable<Fn, Args...> )
    static auto install_queue_and_call(Fn &&fn, Args &&... args) {
        auto prev = std::exchange(instance, &queue_impl::instance);
        auto x = trailer([&]{
            instance->flush_queue();
            instance = prev;
        });
        return fn(std::forward<Args>(args)...);

    }

    ///Installs queue and resume coroutine
    /**
     * Function always install a new queue, even if there is an active queue.
     * This nested queue is cleared before the function returns. Coroutines
     * enqueued to previous queue are not schedulued untile the nested queue is
     * flushed()
     *
     * @param h coroutine to resume after queue is installed
     */
    static void install_queue_and_resume(std::coroutine_handle<> h) {
        install_queue_and_call([](std::coroutine_handle<> h){
            h.resume();
        }, h);
    }


    ///resume in queue
    static void resume(std::coroutine_handle<> h) noexcept {

        if (instance) {
            assert("Attempt to resume empty handle " && h);
            instance->_queue.push(h);
        } else {
            install_queue_and_resume(h);
        }
    }

    struct initial_awaiter {
        //initial awaiter is called with instance, however this instance is not used here
        //because the object is always empty
        initial_awaiter(coro_queue &) {}
        //coroutine don't need to be temporary suspended if there is an instance of the queue
        static bool await_ready() noexcept {return is_active();}
        //we need to install queue before the coroutine can run
        //this is the optimal place
        //function installs queue and resumes under the queue
        static void await_suspend(std::coroutine_handle<> h) noexcept {
            install_queue_and_resume(h);
        }
        //always empty
        static constexpr void await_resume() noexcept {}
    };

    static constexpr auto resume_handle(std::coroutine_handle<> h) {
        return h;
    }

    static std::coroutine_handle<> resume_handle_next() noexcept {
        if (instance && !instance->_queue.empty()) {
            auto h = instance->_queue.front();
            instance->_queue.pop();
            return h;
        } else {
            return std::noop_coroutine();
        }
    }

    static thread_local queue_impl *instance;

    static bool can_block() {
        return instance == nullptr || instance->_queue.empty();
    }

    static constexpr bool initialize_policy() {return true;}
};

inline thread_local coro_queue::queue_impl *coro_queue::instance = nullptr;
inline thread_local coro_queue::queue_impl coro_queue::queue_impl::instance;

/// co_await pause();
/**
 * Pauses current coroutine and resumes coroutine next in the queue. If there is none
 * such, continues in current coroutine
 */
struct pause: std::suspend_always {
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        auto &queue = coro_queue::instance->_queue;
        queue.push(h);
        h = queue.front();
        queue.pop();
        return h;
    }
};

}



#endif /* SRC_COCLS_CORO_QUEUE_H_ */
