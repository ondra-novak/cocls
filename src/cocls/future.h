/**
 * @file future.h
 */
#pragma once
#ifndef SRC_cocls_FUTURE_H_
#define SRC_cocls_FUTURE_H_

#include "awaiter.h"
#include "exceptions.h"
#include "with_allocator.h"


#include <assert.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>



namespace cocls {

/*
 *     │                                │
 *     │owner──────────┐                │owner ──┐
 *     │               ▼                │        │
 *     │           ┌────────┐           ▼        │
 *     ▼           │        │      ┌─────────┐   │
 * co_await ◄──────┤ Future │◄─────┤ Promise │◄──┘
 *     │           │        │      └────┬────┘
 *     │           └────────┘           │
 *     │                                │
 *     │                                │
 *     ▼                                ▼
 */

///Future variable
/** Future variable is variable, which receives value in a future.
 *
 *  This class implements awaitable future variable. You can co_await on the variable,
 *  the coroutine is resumed, once the variable is set.
 *
 *  To set the variable, you need to retrieve a promise object as first. There can
 *  be only one promise object. It is movable only. Once the promise object is created,
 *  the future must be resolved before its destruction. The reason for such ordering is
 *  that neither future nor promise performs any kind of allocation. While future
 *  sits in coroutine's frame, the promise is just pointer which can be moved around,
 *  until it is used to set the value, which also invalidates the promise to prevent further
 *  attempts to set value.
 *
 *  Neither promise, nor future are MT Safe. Only guaranteed MT safety is for setting
 *  the value, which can be done from different thread, it also assumes, that value
 *  is not read before it is resolved.
 *
 *  The future itself is not movable nor copyable. To return future from a function, it
 *  must be constructed as return expression. The future can be constructed using
 *  a function which receives promise as argument. This function is called during
 *  construction of the future and can be used to initialize an asynchronous operation.
 *
 *  @code
 *  return future<int>([&](auto promise){
 *      begin_async(std::move(promise));
 *  });
 *  @endcode
 *
 *  You can also declare a coroutine of the type future_coro to construct the future with
 *  such a coroutine. The coroutine is started during construction of the future
 *
 *  @code
 *  future_coro<int> do_work() {...}
 *
 *  return future<int>(do_work());
 *  @endcode
 *
 *    Future can be awaited by multiple awaiters. However you need to ensure MT
 *  safety by proper synchronization. For example when there are multiple awaiters,
 *  ensure, that no awaiter wants to move the result outside of future. Also ensure,
 *  that future can't be destroyed after it is awaited. For multiple awaiting
 *  is recommended to use make_shared
 *
 *
 * @note Future can act as optional. If the promise is dropped without being called, the
 * future is set resolved, but without setting the value. In this case, accessing the
 * future cause the exception "value_not_ready_exception". However, you can detect this
 * situation before. If you store future in a variable, you can call has_value() which
 * can be also co_await-ed. The behavoir is the same as co_await the future itself,
 * but now, the result is true if the value has been set, or false if not.
 *
 * @tparam T type of future variable. The type can be void when no value is stored. Such future
 * can be used for synchronozation only (however, it can still capture an exception). It is
 * also possible to set T as reference (T &). In such case, the future carries reference, not
 * the value, and the variable must be stored until the consument retrieves the value. This
 * allows to skip copying. Alse note, that future<T&> can be used to construct future<T>, you
 * just need to use construct-by-lambda.
 *
 */
template<typename T>
class future;
///Promise
/**
 * Promise is movable only object which serves as pointer to future to be set.
 * Its content is valid until the object is used to set the value, then it becomes
 * invalid. If this object is destroyed without setting the value of the associated
 * future, the future is resolved with the exception "await_canceled_exception"
 */
template<typename T>
class promise;

template<typename T>
class async_promise;


///Use value drop to drop promise manually
enum DropTag {drop};

///tags all futures, you can use std::base_of<future_tag, T> to determine, whether object
/// is future
class future_common {
public:
    enum class State {
        not_value,
        value,
        value_ref,
        exception
    };

    future_common() = default;
    future_common(awaiter *awt, State state):_awaiter(awt), _state(state) {}

    ///determines, whether future is initialized
    /**
     * @retval false the future has been created right now, there is no value, no
     * pending promise. You can destroy future, you can call get_promise()
     * @retval true the future is already initialized, it can have pending promise
     * or a can be already resolved.
     */
    bool initialized() const {
        return _awaiter.load(std::memory_order_relaxed) == &awaiter::instance;
    }

    ///determines, whether future is pending. There is associated promise.
    /**
     * @retval true the future is pending, can't be destroyed. There is associated
     * promise, which points to the future
     * @retval false the future is either resolved or not yet initialized
     */
    bool pending() const  {
        auto s = _awaiter.load(std::memory_order_relaxed);
        return s != &awaiter::instance && s!= &awaiter::disabled;
    }

    ///determines, whether result is already available
    /**
     * @retval true result is available and can be retrieved. This includes state
     * when promise has been dropped, so the future has actually no value, but it
     * is in resolved state.
     * @retval false the future has no value, it could be not-initialize or pending
     */
    bool ready() const {
        return _awaiter.load(std::memory_order_acquire) == &awaiter::disabled;
    }

    ///Determines whether future can be waited or co_awaited
    /** 
     * @retval true future can be waited or co_awaited
     * @retval false future cannot be waited or co_awaited because there is no promise associated with
     * the object nor the value */
    bool waitable() const noexcept {
        auto x = this->_awaiter.load(std::memory_order_relaxed);
        return x && x != &awaiter::instance;
    }

    bool joinable() const noexcept {
        return waitable();
    }



    ///subscribes awaiter, which is signaled when future is ready
    /**
     * @param awt awaiter to subscribe
     * @retval true awaiter subscribed and waiting
     * @retval false awaiter not subscribed, the result is already available
     */
    bool subscribe(awaiter *awt) {
         return awt->subscribe_check_ready(_awaiter, awaiter::disabled);
    }

    ~future_common() {
        assert("Destroy of pending future" && !pending());
    }



protected:
    mutable awaiter_collector _awaiter = nullptr;
    State _state=State::not_value;
};

template<typename T>
class [[nodiscard]] future: public future_common {
public:


    using promise_object = promise<T>;

    using value_type = T;
    using value_type_ptr = std::add_pointer_t<std::remove_reference_t<value_type> >;
    using reference = std::add_lvalue_reference_t<value_type>;
    using const_reference = std::add_lvalue_reference_t<std::add_const_t<value_type> >;
    static constexpr bool is_void = std::is_void_v<value_type>;
    static constexpr bool is_ref = !is_void && std::is_reference_v<value_type>;
    using value_storage = std::conditional_t<is_void, int,
                    std::conditional_t<is_ref,value_type_ptr,value_type> >;
    using ptr_storage = std::conditional_t<is_void, int,  value_type_ptr>;
    using promise_t = promise<T>;

    //shortcut to allow future coroutines
    using promise_type = async_promise<T>;

    class __SetValueTag {};
    class __SetReferenceTag {};
    class __SetExceptionTag {};
    class __SetNoValueTag {};

    ///construct empty future
    /**
     * It can be used manually. You need to obtain promise by calling the function get_promise(), then
     * future can be awaited
     */
    future():future_common(&awaiter::instance, State::not_value) {};

    ///construct future, calls function with the promise
    /**
     * @param init function to start asynchronous operation and store the promise which is
     * eventually resolved.
     *
     * constructor can be used in return expresion. You can omit the constructor name itself as
     * the constructor is intentionally declared without explicit keyword
     */
    template<typename Fn>
    CXX20_REQUIRES(std::invocable<Fn, promise<T> >)
    future(Fn &&init) {
        init(promise<T>(*this));
    }

    template<typename Fn>
    CXX20_REQUIRES(ReturnsFuture<Fn, T>)
    future(Fn &&init) {
        new(this) auto(init());
    }


    template<typename ... Args>
    future(__SetValueTag, Args && ... args)
        :future_common(&awaiter::disabled, State::value)
        ,_value(std::forward<Args>(args)...) {}

    template<typename ... Args>
    future(__SetReferenceTag,  ptr_storage v)
        :future_common(&awaiter::disabled, State::value)
        ,_ptr_value(v) {}


    future(__SetExceptionTag, std::exception_ptr e)
        :future_common(&awaiter::disabled,State::exception)
        ,_exception(std::move(e)) {}

    future(__SetNoValueTag)
        :future_common(&awaiter::disabled,State::not_value) {}


    ///Resolves future by a value
    template<typename ... Args>
    static future<T> set_value(Args && ... args) {
        if constexpr(is_ref) {
            return future<T>(__SetReferenceTag(), &args...);
        } else {
            return future<T>(__SetValueTag(), std::forward<Args>(args)...);
        }
    }

    ///Resolves future by an exception
    static future<T> set_exception(std::exception_ptr e) {
        return future<T>(__SetExceptionTag(), std::move(e));
    }

    ///Sets future to state not-value. The future is ready, but has no value
    static future<T> set_not_value() {
        return future<T>(__SetNoValueTag());
    }




    ///retrieves promise from unitialized object
    promise<T> get_promise() {
        [[maybe_unused]] auto cur_awaiter = _awaiter.exchange(nullptr, std::memory_order_relaxed);
        assert("Invalid future state" && cur_awaiter== &awaiter::instance);
        return promise<T>(*this);
    }

    ///construct future from result returned by a function (can be lambda)
    /**
     * @param fn function with zero argument and returns the same type
     *
     * @note future is destroyed and recreated. If an exception is thrown from the
     * function, the future is resolved with that exception
     */
    template<typename Fn>
    CXX20_REQUIRES(ReturnsFuture<Fn, T>)
    void result_of(Fn &&fn) noexcept {
        this->~future();
        try {
            new(this) auto(fn());
        } catch(...) {
            new(this) future<T>();
            get_promise()(std::current_exception());
        }
    }

    ///same as result_of
    template<typename Fn>
    CXX20_REQUIRES(ReturnsFuture<Fn, T>)
    future<T> &operator<<(Fn &&fn) noexcept {
        result_of(std::forward<Fn>(fn));
        return *this;
    }

    ///destructor
    ~future() {
        switch (_state) {
            default:break;
            case State::value: _value.~value_storage();break;
            case State::exception: _exception.~exception_ptr();break;
        }
    }

    ///retrieves result value (as reference)
    /**
     * @return reference to the value, you can modify the value or move the value out.
     * Note if there are multiple awaiters, every awaiter can modify or move value. Keep
     * this in mind.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     */
    reference value() {
        switch (_state) {
            default:
                if (pending())
                    throw value_not_ready_exception();
                else
                    throw await_canceled_exception();
            case State::exception:
                std::rethrow_exception(_exception);
                break;
            case State::value:
                if constexpr(!is_void) {
                    if constexpr(is_ref) {
                        return *_value;
                    }
                    else {
                        return _value;
                    }
                }
                break;
            case State::value_ref:
                if constexpr(!is_void) return *_ptr_value;
                break;
        }
    }

    ///retrieves result value (as reference)
    /**
     * @return const reference to the value.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     */
    const_reference value() const {
        switch (_state) {
            default:
                if (pending())
                    throw value_not_ready_exception();
                else
                    throw await_canceled_exception();
            case State::exception:
                std::rethrow_exception(_exception);
                break;
            case State::value:
                if constexpr(!is_void) {
                    if constexpr(is_ref) {
                        return *_value;
                    }
                    else {
                        return _value;
                    }
                }
                break;
            case State::value_ref:
                if constexpr(!is_void) return *_ptr_value;
                break;
        }
    }


    ///retrieves result value (as reference). Waits synchronously if the value is not ready yet
    ///
    /**
     * @return reference to the value, you can modify the value or move the value out.
     * Note if there are multiple awaiters, every awaiter can modify or move value. Keep
     * this in mind.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     * @note in debug build, using wait() is reported as an error. To override, use force_wait();
     */
    reference wait() {
        return co_awaiter<future<T> >(*this).wait();
    }

    ///Same as wait()
    /**@see wait() */
    decltype(auto) join() {return wait();}


    ///Synchronize with future, but doesn't pick value or explore its state
    /**
     * Function waits synchronously until future is resolved, when continues. It
     * doesn't access the value, it doesn't thrown exception
     */
    void sync() const noexcept {
        co_awaiter<future<T> >(*const_cast<future<T> *>(this)).sync();
    }

   ///wait() but with not check
    reference force_wait() {
        return co_awaiter<future<T> >(*this).force_wait();
    }

   ///sync() but with not check
    void force_sync() const noexcept {
        co_awaiter<future<T> >(*const_cast<future<T> *>(this)).force_sync();
    }


    ///Wait asynchronously, return value
    /**
     * The operator retrieves awaiter which can be co_awaiter. The co_await operator
     * can return following:
     *
     * @return reference to the value, you can modify the value or move the value out.
     * Note if there are multiple awaiters, every awaiter can modify or move value. Keep
     * this in mind.
     *
     * @exception value_not_ready_exception accessing the future in not ready state
     * throws this exception. This exception can be thrown also if the future is
     * resolved, but with no value when promise has been dropped (as an broken promise)
     * @exception any if the future is in exceptional state, the stored exception is
     * thrown now
     *
     * @note accessing the value is not MT-Safe.
     */
    co_awaiter<future<T> > operator co_await() {return *this;}


    ///has_value() awaiter return by function has_value()
    class [[nodiscard]] awaitable_bool: public co_awaiter<future<T> > {
    public:
        using co_awaiter<future<T> >::co_awaiter;

        bool await_ready() noexcept {return this->_owner.ready();}
        bool await_resume() noexcept {return this->_owner._state != State::not_value;}
        operator bool() const {
            if (!this->_owner.ready()) this->_owner.sync();
            return this->_owner._state != State::not_value;
        }
    };


    ///Asks whether the future has value
    /**
     * @retval true future has value or exception
     * @retval false future has no value
     *
     * @note if called on pending future, it acts as wait(), so it blocks.
     *
     * @note function is awaitable. You can co_await has_value() which suspend
     * the coroutine until the value is ready
     */
    awaitable_bool has_value() const {
        return awaitable_bool(*const_cast<future<T> *>(this));
    }

    ///Asks whether the future has value
    /**
     * @retval true future has value or exception
     * @retval false future has no value
     *
     * @note if called on pending future, it acts as wait(), so it blocks
     * @see wait()
     */
    operator bool() const {
        return has_value();
    }

    ///Asks whether the future has value
    /**
     * @retval true future has value or exception
     * @retval false future has no value
     *
     * @note if called on pending future, it acts as wait(), so it blocks
     * @see wait()
     */
    bool operator!() const {
        return !has_value();
    }

    ///Dereference - acts as wait()
    /** @see wait() */
    reference operator *() {
        return wait();
    }

protected:
    friend class co_awaiter<future<T> >;
    friend class promise<T>;

    template<typename A>
    friend class async_promise;


    union {
        value_storage _value;
        ptr_storage _ptr_value;
        std::exception_ptr _exception;

    };


    void set_ref(value_storage x) {
        assert("Future is ready, can't set value twice" && _state == State::not_value);
        _ptr_value = x;
        _state = State::value_ref;
    }

    template<typename ... Args>
    void set(Args && ... args) {
        assert("Future is ready, can't set value twice" && _state == State::not_value);
        if constexpr(is_ref) {
            set_ref(&args...);
            return;
        } else if constexpr(!is_void) {
            new(&_value) value_type(std::forward<Args>(args)...);
        }
        _state = State::value;
    }


    void set(std::exception_ptr e) {
        assert("Future is ready, can't set value twice" && _state == State::not_value);
        new (&_exception) std::exception_ptr(std::move(e));
        _state = State::exception;
    }

    auto resolve() {
        return awaiter::resume_chain_set_ready(_awaiter, awaiter::disabled);
    }

};

///promise object - can be obtained by future<>::get_promise() or during construction of the future
/**
 * The promise object acts as function (invokable). It is movable, not copyable. If it is destroyed
 * without calling, the associated future is resolved with no value, which causes that exception is thrown.
 * The promise can be called concurrently where only first call is accepted, other are ignored.
 *
 * The promise is invokable as constructor of returned value with exception - the promise can be
 * called with std::exception_ptr which causes that future throws this exception.
 *
 * @tparam T
 */
template<typename T>
class promise {
public:

    using future_t = future<T>;
    using reference_type = std::add_lvalue_reference_t<typename future<T>::value_storage>;

    ///construct empty promise - to be assigned
    promise():_owner(nullptr) {}
    ///construct promise pointing at specific future
    explicit promise(future_t &fut):_owner(&fut) {}
    ///promise is not copyable
    promise(const promise &other) =delete;
    ///promise can be moved
    promise(promise &&other):_owner(other.claim()) {}
    ///destructor
    ~promise() {
        auto m = _owner.load(std::memory_order_relaxed);
        if (m) m->resolve();

    }
    ///promise cannot assignment by copying
    promise &operator=(const promise &other) = delete;
    ///promise can be assigned by move
    promise &operator=(promise &&other) {
        if (this != &other) {
            set_value(drop);
            _owner = other.claim();
        }
        return *this;
    }



    ///construct the associated future
    /**
     * @param args arguments to construct the future's value - same as its constructor. For
     * promise<void> arguments are ignored
     * @retval true success (race won)
     * @retval false promise is already claimed (race lost)
     *
     * @note return value is awaitable (recommended to co_await result in coroutine)
     */
    template<typename ... Args>
    suspend_point<bool> operator()(Args && ... args) {
        return set_value(std::forward<Args>(args)...);
    }

    ///construct the associated future
    /**
     * @param args arguments to construct the future's value - same as its constructor. For
     * promise<void> arguments are ignored
     * @retval true success (race won)
     * @retval false promise is already claimed (race lost)
     *
     * @note return value is awaitable (recommended to co_await result in coroutine)
     */
    template<typename ... Args>
    suspend_point<bool> set_value(Args && ... args) {
        auto m = claim();
        if (m) {
            m->set(std::forward<Args>(args)...);
            return suspend_point<bool>(m->resolve(), true);
        }
        return suspend_point<bool>(false);
    }

    ///Set value DropTag to drop promise manually
    /**
      * @note return value is awaitable (recommended to co_await result in coroutine)
      * */
    suspend_point<bool> set_value(DropTag) {
        auto m = claim();
        if (m) {
            return suspend_point<bool>(m->resolve(), true);
        }
        return suspend_point<bool>(false);
    }

    suspend_point<bool> reference(reference_type value) {
        auto m = claim();
        if (m) {
            m->set_ptr(&value);
            return suspend_point<bool>(m->resolve(), true);
        }
        return suspend_point<bool>(false);
    }

    ///Sets exception
    /**
      * @note return value is awaitable (recommended to co_await result in coroutine)
      * */
    suspend_point<bool> set_exception(std::exception_ptr e) {
        return set_value(e);
    }

    ///Returns true, if the promise is valid
    operator bool() const {
        return _owner != nullptr;
    }

    ///Returns true, if the promise is not valid
    bool operator !() const {
        return _owner == nullptr;
    }

    ///capture current exception
    bool unhandled_exception()  {
        return set_exception(std::current_exception());
    }

    ///claim this future as pointer to promise - used often internally
    future<T> *claim() const {
        return _owner.exchange(nullptr, std::memory_order_relaxed);
    }

    ///Retrieves promise identifier
    /** This helps to construct identifier, which can be used later to find promise
     * in some kind of containers. The empty promise's identifier is nullptr.
     *
     * @return identifier. Note the idenitifer is construct from pointer to associated
     * future. Do not cast the pointer to future to access the future directly. Use
     * claim() insteaD.
     *
     */
    const void *get_id() const {return _owner;}

    ///Bind arguments but don't resolve yet. Return function, which can
    /// be called to resolve the future
    template<typename ... Args>
    auto bind(Args &&... args) {
        return [p = std::move(*this),args = std::tuple<std::decay_t<Args>...>(std::forward<Args>(args)...)]() mutable {
            return std::apply(std::move(p),std::move(args));
        };
    }



protected:

    ///to allows direct access from derived classes
    /** Sets the future value, but doesn't resolve */
    template<typename ... Args>
    static void set(future<T> *what,  Args && ... args) {
        what->set(std::forward<Args>(args)...);
    }

    ///to allows direct access from derived classes
    /** resolves future */
    static void resolve(future<T> *what) {
        what->resolve();
    }

    ///to allows direct access from derived classes
    /** resolves future from await_suspend or resume_handle*/
    static std::coroutine_handle<> resolve_resume(future<T> *what) {
        return what->resolve_resume();
    }

    mutable std::atomic<future<T> *>_owner;

    ///construct the associated future, suspend current coroutine and switch to other coroutine
    /**
     * @param args arguments to construct the future's value - same as its constructor. For
     * promise<void> arguments are ignored
     * @return handle to coroutine to be resumed.
     *
     * Main purpose of this function is to be called during await_suspend(). During
     * this function, the coroutine can resolve the future and then execution is switched
     * to associated coroutine which can process the result
     *
     * @note function returns coroutine handle to resume. Do not discard result, if you
     * has no use for the result, call resume() on the result
     */
    template<typename ... Args>
    [[nodiscard]] std::coroutine_handle<> set_value_and_suspend(Args && ... args) {
        auto m = claim();
        if (m) {
            m->set(std::forward<Args>(args)...);
            return m->resolve_resume();
        } else {
            return std::noop_coroutine();
        }
    }

    [[nodiscard]] std::coroutine_handle<> set_value_and_suspend(DropTag) {
        auto m = claim();
        if (m) {
            return m->resolve_resume();
        } else {
            return std::noop_coroutine();
        }
    }

    ///drop promise during current coroutine is being suspended
    /**
     * @return handle to coroutine to be resumed
     */
    [[nodiscard]] std::coroutine_handle<> drop_and_suspend() {
        auto m = claim();
        if (m) {
            return m->resolve_resume();
        } else {
            return std::noop_coroutine();
        }
    }
};


///Promise with default value
/** If the promise is destroyed unresolved, the default value is set to the future */
template<typename T>
class promise_with_default: public promise<T> {
public:
    using super = promise<T>;
    using promise<T>::promise;

    template<typename ... Args>
    promise_with_default(super &&prom, Args &&... args)
        :promise<T>(std::move(prom)),def(std::forward<Args>(args)...) {}
    ~promise_with_default() {
        this->set_value(std::move(def));
    }
    promise_with_default(promise_with_default &&other) = default;
    promise_with_default &operator=(promise_with_default &&other) {
        if (this != &other) {
            promise<T>::operator=(std::move(other));
            def = std::move(def);
        }
        return *this;
    }
protected:
    T def;

};

///Promise with default value
/**If the promise is destroyed unresolved, the default value is set to the future
 *
 * @tparam T type, must be integral type
 * @tparam val default value
 */
template<typename T, T val>
class promise_with_default_v: public promise<T> {
public:
    using super = promise<T>;
    using promise<T>::promise;
    promise_with_default_v() = default;
    promise_with_default_v(promise_with_default_v &&other) = default;
    promise_with_default_v &operator=(promise_with_default_v &&other) = default;
    ~promise_with_default_v() {
        this->set_value(val);
    }
    promise_with_default_v(super &&p):promise<T>(std::move(p)) {}
};

///Promise with default value - constant is specified in template paramater
/**If the promise is destroyed unresolved, the default value is set to the future
 *
 * @tparam T type
 * @tparam val const pointer to default value, must have external linkage
 */
template<typename T, const T *val>
class promise_with_default_vp: public promise<T> {
public:
    using super = promise<T>;
    using promise<T>::promise;
    promise_with_default_vp() = default;
    promise_with_default_vp(promise_with_default_vp &&other) = default;
    promise_with_default_vp &operator=(promise_with_default_vp &&other) = default;
    ~promise_with_default_vp() {
        this->set_value(*val);
    }
    promise_with_default_vp(super &&p):promise<T>(std::move(p)) {}
};


///Futures with callback function
/**
 * When future is resolved a callback function i called
 * @tparam T type of value
 * @tparam Fn function type
 *
 * This class is intended to be used in classes as member variables, to avoid
 * memory allocation - because the future must be allocated somewhere.
 *
 * If you have no such place, use more convenient function make_promise
 *
 * @see make_promise()
 */
template<typename T, typename Fn>
class future_with_cb: public future<T>, public awaiter {
public:

    ///Construct a future and pass a callback function
    future_with_cb(Fn &&fn):_fn(std::forward<Fn>(fn)) {
        this->_awaiter = this;
        set_resume_fn([](awaiter *me, auto) noexcept -> suspend_point<void> {
            auto _this = static_cast<future_with_cb<T,Fn> *>(me);
            _this->_fn(*_this);
            delete _this;
            return {};
        });
    }

    promise<T> get_promise() {
        return promise<T>(*this);
    }

    template<typename Factory>
    CXX20_REQUIRES(ReturnsFuture<Factory, T>)
    future_with_cb &operator << (Factory &&fn) {
        future<T>::operator<<(std::forward<Factory>(fn));
        return *this;
    }

    virtual ~future_with_cb() = default;

protected:
    Fn _fn;

};


///Extends the future_with_cb with ability to be allocated in a storage
template<typename T, typename Storage, typename Fn>
using future_with_cb_no_alloc = custom_allocator_base<Storage, future_with_cb<T, Fn> >;


/**@{*/
///Makes callback promise
/**
 * Callback promise cause execution of the callback when promise is resolved.,
 * This function has no use in coroutines, but it can allows to use promises in normal
 * code. Result object is normal promise.
 *
 * There is also only one memory allocation for whole promise and the callback function.
 *
 * @tparam T type of promise
 * @param fn callback function. Once the promise is resolved, the callback function receives
 * whole future<T> object as argument (as reference). It can be used to retrieve the value from it
 *
 * @return promise<T> object
 *
 * @note callback is executed only after all instances of the promise are destroyed. This helps
 * to reduce side-effect which could potential happen during setting the value of the promise. So
 * execution is postponed. However ensure, that when promise is resolved, the promise instance
 * is being destroyed as soon as possible
 *
 * @see future<T>::get()
 */
template<typename T, typename Fn>
promise<T> make_promise(Fn &&fn) {
    auto f = new future_with_cb<T, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}


template<typename T, typename Fn, typename Storage>
promise<T> make_promise(Fn &&fn, Storage &storage) {
    auto f = new(storage) future_with_cb_no_alloc<T, Storage, Fn>(std::forward<Fn>(fn));
    return f->get_promise();
}
/**@}*/




///discard result of a future
/**
 * @param fn function which returns a future. Function is called. Return value
 * is discarded, so it no longer need to awaited.
 *
 * @note function allocates memory for result, which is destroyed when future is resolved
 *
 *
 * @code
 * discard([&]{function_returning_future();});
 * @endcode
 */
template<typename Fn>
void discard(Fn &&fn) {
    using fut_type = std::decay_t<decltype(std::declval<Fn>()())>;
//    using T = typename fut_type::value_type;
    class Awt: public awaiter {
    public:
        Awt(Fn &&fn, bool &waiting):_fut(fn()) {
            set_resume_fn(&fin);
            waiting = (_fut.operator co_await()).subscribe(this);
        }

        static suspend_point<void> fin(awaiter *me, void *) noexcept {
            auto _this = static_cast<Awt *>(me);
            delete _this;
            return {};
        }
    protected:
        fut_type _fut;
    };

    bool w = false;
    auto x = new Awt(std::forward<Fn>(fn), w);
    if (!w) x->resume();
}

///Implements future awaiter, which calls a member function when future is set
/**
 * This object can replace a coroutine for simple usage, especially when an object
 * need to convert a future of asynchronous operation, or if can easy handle
 * state of asynchronous operation. Great benefit of this replacement is that
 * this object has known size during compile time and can be declared as member
 * variable of associated object.
 *
 * @tparam T Type of future which is handled by this awaiter (future<T>)
 * @tparam Obj Object which handles completion of the future
 * @tparam fn pointer to member function, which handles completion of the future. The
 * function has following prototype: suspend_point<void> fn(future<T> &) noexcept
 *
 * You need to construct this object with reference to the owner object. To initiate
 * asynchronous operation and to capture resulting future, you need to use
 * the operator << (similar for future<T>) and pass a function or a lambda function,
 * which returns a future to be captured.
 *
 * Once future is complete, the function fn is called with the future instance. The
 * future passed as argument is already completed.
 *
 * @note The function to be called must be declared as noexcept. However, accessing
 * the future can throw an exception. The function must properly handle the exception
 *
 * @note Disadvantage: Contrary to coroutines, the function is called immediately
 * once the completion is done. No suspend_point is involved. This can be an issue
 * for complex state handling. In this case, it is better to use the coroutine
 *
 * @note Object is not movable nor copyable
 *
 */
template<auto fn> class call_fn_future_awaiter;

template<typename T, typename Obj,suspend_point<void> (Obj::*fn)(future<T> &) noexcept>
class call_fn_future_awaiter<fn>: public awaiter {
public:
    ///construct the object
    /**
     * @param owner reference to object which handles completion event
     */
    call_fn_future_awaiter(Obj &owner) {
        set_resume_fn(wakeup, &owner);
    }

    call_fn_future_awaiter(Obj *owner) {
        set_resume_fn(wakeup, owner);
    }


    ///Run specified function or lambda function, capture future<> result and register the callback (atomically)
    /**
     * Function captures a returning future<> and performs operation 'await' on it.
     * Once operation is complete, the specified member function is called.
     *
     * @param xfn a function to execute
     *
     *
     */
    template<typename Fn>
    CXX20_REQUIRES(ReturnsFuture<Fn, T>)
    void operator<<(Fn &&xfn) {
        _fut << std::forward<Fn>(xfn);
        if (!_fut.subscribe(this)) {
            this->resume();
        }
    }
protected:
    future<T> _fut;
    static suspend_point<void> wakeup(awaiter *me, void *user_ctx) noexcept {
        Obj *owner = reinterpret_cast<Obj *>(user_ctx);
        auto _this = static_cast<call_fn_future_awaiter *>(me);
        return ((*owner).*fn)(_this->_fut);
    }
};

///This object directly implements future along with its awaiter
/**
 * The main purpose for this object is to allow use operator co_await
 * for class which can be costructed and immediatelly converted to
 * an awaiter.
 *
 * Instead
 * @code
 * AsyncCalcObj() -> future<T>() -> co_awaiter<future<T> >
 * @endcode
 *
 * You can use sequence
 * @code
 * AsyncCalcObj() -> future_awaiter<T>()
 * @endcode
 *
 * for example following code
 * @code
 * class AsyncCalcObj {
 * public:
 *      future<T> run_async();
 *
 *      future_awaiter<T> operator co_await() {
 *          return [&](return run_async();};
 *      }
 * };
 *
 * co_await AsyncCalcObj(...)
 * @endcode
 * The above code allows to run_async() function by simply co_await on
 * the object's instance.
 *
 * @tparam T returned value
 */
template<typename T>
class future_awaiter: public cocls::future<T>, public cocls::co_awaiter<cocls::future<T> > {
public:
    template<typename ... Args>
    future_awaiter(Args && ... args)
        :cocls::future<T>(std::forward<Args>(args)...)
        ,cocls::co_awaiter<cocls::future<T> >(static_cast<cocls::future<T> &>(*this)) {}
};




}
#endif /* SRC_cocls_FUTURE_H_ */

