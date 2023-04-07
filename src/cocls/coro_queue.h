#pragma once
#ifndef SRC_COCLS_CORO_QUEUE_H_
#define SRC_COCLS_CORO_QUEUE_H_


#include "common.h"

#include <cassert>
#include <deque>
#include <utility>
#include <vector>

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
        std::deque<std::coroutine_handle<> > _queue;

        void flush_queue() noexcept {
            while (!_queue.empty()) {
                auto h = std::move(_queue.front());
                _queue.pop_front();
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
            instance->_queue.push_back(h);
        } else {
            install_queue_and_resume(h);
        }
    }


    /// swap coroutine - store coroutine handle to queue and retrieve another coroutine handle
    /**
     *  @param h coroutine being suspended
     *  @return coroutine to resume
     *
     *  @note if no queue is installed, function returns argument
     */
    static std::coroutine_handle<> swap_coroutine(std::coroutine_handle<> h) noexcept {
        if (instance) {
            instance->_queue.push_back(h);
            h = instance->_queue.front();
            instance->_queue.pop_front();
            return h;
        } else {
            return h;
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
            instance->_queue.pop_front();
            return h;
        } else {
            return std::noop_coroutine();
        }
    }

    template<typename Fn>
    static auto create_suspend_point(Fn &&fn);


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
        queue.push_back(h);
        h = queue.front();
        queue.pop_front();
        return h;
    }
};

///Suspend point is object, which can be optionally awaited to suspend current coroutine and resume coroutines ready to be run
/**
 * The main purpose of suspend_point is to be returned from a function. Such function can be co_awaited for result which
 * is carried by the instance. However as a side effect, it can also carry a one or more coroutine handles which are
 * ready to be resumed. By co_await on a suspend_point causes to suspension of current coroutine and switch
 * execution to a coroutine associated with the finished function.
 *
 * Example: By setting a value to a promise causes that an awaiting coroutine becomes ready to run. Result of
 * operation which sets the promise is suspend_point which can be co_awaited. This causes that ready coroutine is resumed
 *
 * @code
 * promise<int> prom=...;
 * suspend_point<bool> sp = prom(42); //set value to a promise
 * co_await sp; //switch to ready coroutine
 * @endcode
 *
 * Awaiting on suspend point is optional. If the suspend point is not awaited, but discarded, the futher scheduling of
 * coroutines associated with the suspend point are processed as usual. Inside of coroutine, the execution is scheduled
 * to next suspend or return. In normal thread, the execution is performed immediately interrupting the current thread. By
 * capturing suspend point to a variable allows you to specify optimal place where to resume associated coroutines
 *
 *
 * Instance of suspend_point doesn't allocate a memory until count of ready coroutines cross threshold 4.
 */
template<typename RetVal>
class suspend_point;


template<>
class suspend_point<void> {
private:

    static constexpr std::size_t inline_count = 4;

    struct Allocator {
        using value_type = std::coroutine_handle<>;
        using size_type = std::size_t;
        using difference_type	= std::ptrdiff_t;
        template< class U >
        struct rebind
        {
            typedef Allocator other;
        };

        char _buffer[sizeof(std::coroutine_handle<>)*inline_count];

        Allocator() = default;
        Allocator(Allocator &&a) = delete;
        Allocator(const Allocator &) = delete;
        std::coroutine_handle<> *allocate(std::size_t sz) {
            void *ret;
            if (sz > inline_count) {
                ret = ::operator new(sizeof(std::coroutine_handle<>)*sz);
            } else {
                ret = _buffer;
            }
            return  reinterpret_cast<std::coroutine_handle<> *>(ret);
        }
        void deallocate(std::coroutine_handle<> *ptr, std::size_t) {
            if (reinterpret_cast<char *>(ptr) != _buffer) {
                ::operator delete(ptr);
            }
        }
        friend constexpr bool operator==(const Allocator &, const Allocator &)  {
            return true;
        }
    };
public:
    ///Construct empty suspend point - until there is at least one coroutine, it cannot be awaited
    suspend_point() {_list.reserve(inline_count);}

    suspend_point(std::coroutine_handle<> h) {
        _list.reserve(inline_count);
        _list.push_back(h);
    }
    ///I can't copy suspend point
    suspend_point(const suspend_point &other) = delete;
    ///You can move suspend point
    suspend_point(suspend_point &&other) {
        if (other._list.size() > inline_count) {
            std::swap(other._list, _list);
        } else {
            _list.reserve(inline_count);
            std::copy(other._list.begin(), other._list.end(),std::back_insert_iterator(_list));
            other._list.clear();
        }
    }
    ///You can't assign
    suspend_point &operator=(const suspend_point &other) = delete;
    ///You can't assign
    suspend_point &operator=(suspend_point &&other) = delete;

    ///Destructor - resumes all ready coroutines. This allows to discard the instance
    ~suspend_point() {
        flush();
    }
    /// Determines whether suspend point is empty
    /**
     * @retval true empty
     * @retval false not empty
     */
    bool empty() const {
        return _list.empty();
    }

    ///push coroutine handle to a suspend_point
    /** @param h handle to coroutine.
     *
     * @note function checks for null argument. In this case the argument is ignored. You don't need to check the argument for null
     */
    void push_back(std::coroutine_handle<> h) {
        if (h) _list.push_back(h);
    }
    void push_back(suspend_point<void> &x) {
        for (auto &h: x._list) {
            _list.push_back(h);
        }
        x._list.clear();
    }
    void push_back(suspend_point<void> &&x) {
        push_back(x);
    }

    ///Pop one handle from the suspend_point instance.
    /**
     * This is used for symmetric transfer. Function in called to retrieve return value from await_suspend()
     * If there is no coroutine handle, returns noop_coroutine()
     */
    std::coroutine_handle<> pop() {
        if (_list.empty()) return std::noop_coroutine();
        auto h = _list.back();
        _list.pop_back();
        return h;
    }


    ///Resume all coroutines now
    /**
     * The function respects state of current thread. If the coro_queue is active, then all coroutine in this suspend point
     * are put to the queue and not resumed. In normal thread, all coroutines are resumed
     *
     * In a coroutine, you need to use co_await on suspend point to resume all coroutines ready in this suspend_point instance
     */
    void flush() {
        if (coro_queue::is_active()) {
            while (!_list.empty()) {
                coro_queue::resume(_list.back());
                _list.pop_back();
            }
        } else {
            coro_queue::install_queue_and_call([&]{
                flush();
            });
        }
    }
    ///support for co_await
    bool await_ready() const {return _list.empty();}
    ///support for co_await
    static constexpr void await_resume() noexcept {};
    ///support for co_await
    /**
     *  prepares coroutines to current thread's queue, then suspends current coroutine and
     *  then resumes the first coroutine. Then other coroutines are resumed and finally
     *  suspended coroutine is resumed
     *
     *  @note If there is a coroutine in current thread's queue, it is resumed first similar to how pause() works
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        flush();
        return coro_queue::swap_coroutine(h);
    }

protected:
    std::vector<std::coroutine_handle<> , Allocator> _list;
};

template<typename X>
class suspend_point: public suspend_point<void> {
public:
    ///Constructs empty suspend point, but set its value
    suspend_point(X val)
        :value(std::move(val)) {}
    ///Constructs suspend point by setting a value of untyped suspend point
    suspend_point(suspend_point<void> &&src, X val)
        :suspend_point<void>(std::move(src)), value(std::move(val)) {}

    ///return value
    operator X() {
        return value;
    }
    ///return value
    operator const X() const {
        return  value;
    }
    ///Allows that co_await returns a value
    X& await_resume() noexcept {return value;}

protected:
    X value;
};

///Creates suspens point instance
/**
 * @param fn a function which can make some coroutines to be ready to run. These coroutines
 * are prepared in current thread's queue. These coroutines are then moved to a suspend_point
 * instance. Result of the function is associated with the returned suspend point
 */
template <typename Fn>
inline auto coro_queue::create_suspend_point(Fn &&fn)
{
    if (is_active()) {
        auto sz = instance->_queue.size();
        using ret_v = decltype(fn());
        suspend_point<void> ss;
        if constexpr(std::is_void_v<ret_v>) {
            fn();
            while (instance->_queue.size() > sz) {
                ss.push_back(instance->_queue.back());
                instance->_queue.pop_back();
            }
            return ss;
        } else {
            ret_v v = fn();
            while (instance->_queue.size() > sz) {
                ss.push_back(instance->_queue.back());
                instance->_queue.pop_back();
            }
            return suspend_point<ret_v>(std::move(ss), std::move(v));
        }
    } else {
        return install_queue_and_call([&]{
            return create_suspend_point(std::forward<Fn>(fn));
        });
    }
}

}
#endif /* SRC_COCLS_CORO_QUEUE_H_ */
