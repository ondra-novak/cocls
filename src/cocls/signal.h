
#ifndef SRC_cocls_AWAITABLE_SLOT_H_
#define SRC_cocls_AWAITABLE_SLOT_H_
#include "awaiter.h"
#include "queue.h"
#include "exceptions.h"
#include "function.h"

#include <atomic>
#include <coroutine>
#include <memory>
#include <variant>
#include <type_traits>

namespace cocls {




/*
 * ┌─────────┐          ┌───────────┐     ┌──────────┐     ┌────────────┐               ┌────────────────────┐
 * │         │ co_await │           │     │  signal  │     │            │   fn(<param>) │                    │
 * │   coro  ├─────────►│  emitter  │◄─┬──┤  shared  ├──┬─►│ collector  │◄──────────────┤  signal generator  │
 * │         │          │           │  │  │  state   │  │  │            │               │                    │
 * └─────────┘          └───────────┘  │  └──────────┘  │  └────────────┘               └────────────────────┘
 *                                     │                │
 *                                     │                │
 *                              get_emitter()        get_collector()
 */

///Awaitable signal

template<typename T>
class signal {

    using storage_type = std::conditional_t<std::is_void_v<T>, bool , T>;

    struct state { // @suppress("Miss copy constructor or assignment operator")
        awaiter_collector _chain;
        storage_type *_cur_val = nullptr;
        std::optional<storage_type> _value_storage;

        suspend_point<void> notify_awaiters() {
            return awaiter::resume_chain(_chain);
        }

        ~state() {
            _cur_val = nullptr;
            notify_awaiters();
        }

    };


public:

    ///type of value associated with the signal
    using value_type = T;
    ///reference of value
    using reference = std::add_lvalue_reference_t<T>;
    ///rvalue reference of the value
    using rvalue_reference = std::add_rvalue_reference_t<T>;
    ///base type which contains value type with removed reference, or bool for void
    using base_type = std::conditional_t<std::is_void_v<T>, bool ,std::remove_cvref_t<T> >;

    using rvalue_param = std::conditional_t<std::is_void_v<T>,bool &&,rvalue_reference>;
    using lvalue_param = std::conditional_t<std::is_void_v<T>,bool &,reference>;

    ///signal collector
    /**
     * Is callable object, which accepts a one argument. This argument is value which
     * is associated with the signal. The value is the send to all emitters.
     *
     * If the T is void, then function doesn't accept any argument
     */
    class collector {
    public:

        ///create awaitable slot
        collector(std::shared_ptr<state> state):_state(std::move(state)) {}

        ///wake up all awaiters and pass value constructed by arguments (passed to the constructor)
        /**
         * @param args arguments used to construct value (using constructor). The
         * value is destroyed before return.
         * @return suspend point which can be co_awaited in a coroutine. By co_awaiting the result
         * causes transfering execution to the awaiting emitters. It also returns count of
         * awaiting coroutines as result. In normal thread, you can simply discard the result
         * which suspends the thread executions and immediately resumes the awaiting coroutines
         *
         * @note the function is not MT-Safe, use proper synchronization to achieve mt-safety. Remember
         * you need to protect the returned suspend point as well.
         */
        template<typename ... Args>
        CXX20_REQUIRES(std::is_constructible_v<storage_type, Args...> )
        suspend_point<void> operator()(Args && ... args) const {
            _state->_value_storage.emplace(std::forward<Args>(args)...);
            _state->_cur_val = &(*_state->_value_storage);
            return _state->notify_awaiters();
        }

        ///wake up all awaiters and pass value as rvalue reference
        /**
         * @param val value to broadcast to all awaiters
         * @return suspend point which can be co_awaited in a coroutine. By co_awaiting the result
         * causes transfering execution to the awaiting emitters. It also returns count of
         * awaiting coroutines as result. In normal thread, you can simply discard the result
         * which suspends the thread executions and immediately resumes the awaiting coroutines
         *
         * @note the function is not MT-Safe, use proper synchronization to achieve mt-safety.Remember
         * you need to protect the returned suspend point as well.
         *
         */
        suspend_point<void> operator()(rvalue_param val) const {
            _state->_value_storage.emplace(std::move(val));
            _state->_cur_val = &(*_state->_value_storage);
            return _state->notify_awaiters();
        }

        ///wake up all awaiters and pass value as lvalue reference
        /**
         * @param val value to broadcast to all awaiters
         * @return suspend point which can be co_awaited in a coroutine. By co_awaiting the result
         * causes transfering execution to the awaiting emitters. It also returns count of
         * awaiting coroutines as result. In normal thread, you can simply discard the result
         * which suspends the thread executions and immediately resumes the awaiting coroutines
         *
         * @note the function is not MT-Safe, use proper synchronization to achieve mt-safety.Remember
         * you need to protect the returned suspend point as well.
         * 
         * @note This function doesn't store the value. It just stores reference to the value. Ensure that
         * value remains valid until the all emitters are notified. This can be achieved by discarding the
         * return value or using co_await on it.
         *
         */
        suspend_point<void> operator()(lvalue_param val) const {
            _state->_cur_val = &val;
            return _state->notify_awaiters();
        }

        ///you can convert collector to signal object
        operator signal() {
            return signal(_state);
        }


    public:
        std::shared_ptr<state> _state;
        
    };

    ///awaitable object - you can co_await on it
    /** The emitter is awaitable object, by awaiting on it, the coroutine is suspended
     * and it is resumed right when a value is collected by the collector.
     *
     * @note that during processing the value, the collector is blocked until the coroutine
     * is suspended regardless on reason of suspension. Once the coroutine is suspended,
     * the collector is unblocked and can collect and emit a next value. If the coroutine
     * need to receive this value, the only allowed suspension during processing is suspension
     * on the emitter itself.
     */
    class emitter: public awaiter {
    public:
        ///create empty awaiter
        /** if such awaiter is co_awaited, it always throws exception
         * await_canceled_exception()
         *
         * You can use empty awaiter to assign the instance later, or
         * you can call create_signal, to create signal slot and initialize awaiter
         * at the same point
         *
         */
        emitter() = default;
        ///copy operator just shares the state, but not internal state
        emitter(std::weak_ptr<state> state):_wk_state(std::move(state)) {}
        ///move operator moves everything
        emitter(const emitter &x):_wk_state(x._wk_state) {}
        emitter(emitter &&x) = default;
        emitter &operator=(const emitter &x) {
            if (&x != this) {
                _wk_state = x._wk_state;
            }
            return *this;
        }
        emitter &operator=(emitter &&) = default;

        ///required for co_await
        static constexpr bool await_ready() noexcept {
            return false;
        }
        ///required for co_await
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            auto s = _wk_state.lock();
            if (s) {
                set_handle(h);
                this->subscribe(s->_chain);
                return true;
            }  else {
                return false;
            }
        }

        ///required for co_await
        reference await_resume() {
            auto s = _wk_state.lock();
            if (s) {
                auto v = s->_cur_val;
                if (v) {
                    if constexpr(std::is_void_v<T>) {
                        return;
                    } else {
                        return *v;
                    }
                }
            }
            throw await_canceled_exception();
        }

    protected:
        std::weak_ptr<state> _wk_state;
        std::coroutine_handle<> _h;
    };

    ///get signal emitter
    /** You can pass the result to a coroutine which can co_await in it
     *
     * @return emitter, can be co_awaited if you want to receive a signal
     *
     * @note emitter holds weak reference. You need to hold collector or
     * signal instance to keep emitter connected
     */
    emitter get_emitter() const {
        return emitter(_state);
    }

    ///get signal collector
    /**
     * @return collector, it acts as function, so can be stored in std::function.
     *
     * @note collector holds strong reference. The signal state is held if there
     * is at least one receiver.
     *
     * @note collector is not MT safe. Only one call is allowed at time. Use synchronization!
     */
    collector get_collector() const {
        return collector(_state);
    }

    ///connect callback emitter
    /**
     * @param fn the callback function called for every value. The function should accept
     * a one argument - the value - and returns true to continue receiving signals,
     * or false to stop receiving
     */
    template<typename Fn>
    CXX20_REQUIRES(
            ((!std::same_as<T,void>) && std::constructible_from<bool, decltype(std::declval<Fn>()(std::declval<reference>()))>)
            ||
            ((std::same_as<T,void>) && std::constructible_from<bool, decltype(std::declval<Fn>()())>)
    )
    void connect(Fn &&fn) {

        class Awt: public emitter {
        public:
            Awt(Fn &&fn, std::weak_ptr<state> state)
                    :emitter(state)
                    ,_fn(std::forward<Fn>(fn)) {

                this->set_resume_fn([](awaiter *me, auto) noexcept -> suspend_point<void> {
                   static_cast<Awt *>(me)->resume();                   
                   return {};
                });
            }

            void resume() noexcept {
                auto st = this->_wk_state.lock();
                if (!st) {
                    delete this;
                    return;
                }
                if constexpr(std::is_void_v<T>) {
                    this->await_resume();
                    if (_fn()) {
                        this->subscribe(st->_chain);
                    } else {
                        delete this;
                    }

                } else {
                    if (_fn(this->await_resume())) {
                        this->subscribe(st->_chain);
                    } else {
                        delete this;
                    }
                }
            }

            void initial_reg() {
                auto st = this->_wk_state.lock();
                if (st) {
                    this->subscribe(st->_chain);
                } else {
                    resume();;
                }
            }
        protected:
            Fn _fn;
        };

        auto *x = new Awt(std::forward<Fn>(fn), _state);
        x->initial_reg();
    }

    ///create signals shared state
    signal():_state(std::make_shared<state>()) {}

    ///Extended emitter with ability to hook up with collector to a signal generator
    /**
     * This object is returned by the function hook_up(). To correctly use this object,
     * you must not convert it to emitter
     *
     * @see hook_up
     */
    template<typename Fn>
    class hook_up_emitter: protected emitter{
    public:
        hook_up_emitter(Fn &&fn):_fn(fn),_hooked(false) {}

        using emitter::await_ready;
        using emitter::await_resume;
        bool await_suspend(std::coroutine_handle<> h) {
            if (_hooked) return emitter::await_suspend(h);
            _hooked = true;
            signal s;
            this->_wk_state = s._state;
            emitter::await_suspend(h);
            _fn(s.get_collector());
            return true;
        }
    protected:
        Fn _fn;
        bool _hooked;
    };


    ///Creates a emitter with ability to hook up to an existing signal generator
    /**
     * In some cases, you need to atomically register to a signal generator and suspend
     * the coroutine. This function creates a hook_up_emitter which calls the RegFn
     * (registration function) after the coroutine is suspended on it. The
     * function receives a collector, which can be passed to the signal generator. Once
     * the collector is signaled, the coroutine can be woken up by very first emited
     * signal.
     *
     * To use this emitter, you need just co_await on it for the very first time. For
     *  the further co_awaits the emitter works as usual.
     *
     * @code
     * // inside of a coroutine
     * auto e = signal<int>::hook_up([&](auto collector) {
     *          signal_generator.register_callable(collector);
     * });
     * try {
     *      while(true) {
     *          int val = co_await e;
     *          std::cout << "received: " << e << std::endl;
     *      }
     * catch (const await_canceled_exception &) {
     *      std::cout << "stop" << std::endl;
     * }
     * @endcode
     *
     * You cannot do this other way, because emitter is registered for signal only
     * while the coroutine is suspended. So once you register the collector, you
     * can immediatelly miss the very first emitted signal.
     *
     * @param fn function to be called for register on signal generator. It accepts
     *   collector instance, which is movable
     * @return hook_up_emitter which acts as emitter with ability to perform registration
     * on the very first co_await
     */
    template<typename RegFn>
    CXX20_REQUIRES(std::invocable<RegFn, collector>)
    static auto hook_up(RegFn &&fn) {
        return hook_up_emitter<RegFn>(std::forward<RegFn>(fn));
    }

protected:
    std::shared_ptr<state> _state;
    signal(std::shared_ptr<state> x):_state(std::move(x)) {}


};

}

#endif /* SRC_cocls_AWAITABLE_SLOT_H_ */

