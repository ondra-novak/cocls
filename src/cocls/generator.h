#pragma once
#ifndef SRC_cocls_GENERATOR_H_
#define SRC_cocls_GENERATOR_H_
#include "awaiter.h"
#include "generics.h"
#include "future.h"
#include "iterator.h"
#include "generics.h"

#include <memory>

namespace cocls {


///Generator
/**
 * Generator can be synchronous or asynchronous and can be accessed synchronously
 * and asynchronously. When asynchronous generator is accessed synchronously, then
 * thread which accessing such generator is blocked until the generator generates a value.
 * It is recommended to use asynchronous access for asynchronous generators.
 *
 * Generator is coroutine, which can use co_yield to emit value. It is also possible
 * to send values to the generator, because co_yield can return a value. If this
 * is enabled, it is possible to call co_yield nullptr to read the very first value.
 *
 * The generator can be controlled via iterator and can be used as container for range-for,
 * in all these cases, the generator is accessed synchronously.
 *
 * You can also use functions next() and value() to control the generator. The function
 * next() can be co_await-ed and it always return true - while new item is generated
 * and false, when no more items are generated. To access the result itself, you
 * need to use value()
 *
 * The generator can be also used as callable function, which returns future<>. The
 * future can act as optional, which allows to check, whether generator has new value or
 * not.
 *
 * The generator's coroutine is initialized suspended and it starts on the first access, this
 * is also true for the generator with the argument. To access the argument after the start,
 * the generator need to co_yield nullptr. The generator is full featured coroutine, it
 * can use co_await as it need. However any use of co_await activates asynchronous mode
 * of the generator, which can slightly reduce a performance especially when co_await
 * dosn't actually perform any asynchronous operation.
 *
 * @tparam Ret specifies return value of the call, or type of value, which the generator
 * generates. Can't be void, can't be std::nullptr. If you need to specify no-value, use
 * std::monostate
 * @tparam Arg specifies argument type. This type can be void, which means, that generator
 * doesn't expect any argument
 */
template<typename Ret, typename Arg = void>
class generator {
public:

    ///type of argument
    using arg_type = Arg;
    ///contains true, if the generator doesn't need argument
    static constexpr bool arg_is_void = std::is_void_v<Arg>;
    ///reference to argument or void
    using reference_Arg = std::add_lvalue_reference_t<Arg>;
    ///type of storage of arg, because void cannot be stored, it is stored as std::nullptr_t
    using storage_Arg_ptr = std::conditional_t<arg_is_void,std::nullptr_t, Arg *>;
    ///type of argument passed to a function, which defaults to std::nullptr_t in case, that Arg is void
    using param_Arg = std::conditional_t<arg_is_void,std::nullptr_t, reference_Arg>;

    ///type of iterator
    using iterator = generator_iterator<generator<Ret, Arg> >;

    ///contains coroutine promise
    class promise_type {


        //contains awaiter of caller - which is resumed on co_yield
        awaiter *_caller = {};
        //contains internal awaiter for synchronous access and future<> access
        malleable_awaiter _internal;
        //contains arguments (optional)
        [[no_unique_address]] storage_Arg_ptr _arg = {};
        //contains pointer to return value - value set by co_yield
        Ret *_ret = {};
        //contains exception is any
        std::exception_ptr _exp;
        //contains true when generator is finished
        bool _done = false;
        //blocking flag for synchronous access - contains false when generator is pending
        std::atomic<bool> _block;
        //contains promise if called and there is a future waiting for result
        promise<Ret> _awaiting;

        //function is called when accessing synchronously
        /*
         * @param awt awaiter (not interested)
         * @param user_ptr - used to point to generator's promise type
         * @param h - unused in sync mode
         */
        static suspend_point<void> resume_fn_sync(awaiter *, void *user_ptr) noexcept {
            auto g = reinterpret_cast<promise_type *>(user_ptr);
            g->unblock_sync();
            return {};
        }


        //function is called when access through future-promise
        /*
         * @param awt unused
           @param user_ptr - points to generator's promise type
           @param h - is set to coroutine handle to resume - returned during promise resolution
         *
         */
        static suspend_point<void> resume_fn_future(awaiter *, void *user_ptr) noexcept {
            auto g = reinterpret_cast<promise_type *>(user_ptr);
            return g->unblock_future();
        }

        void unblock_sync() {
            _block.store(true, std::memory_order_release);
            _block.notify_all();
        }

        suspend_point<void> unblock_future() {

            if (done()) return _awaiting(drop);
            else if (_exp) return _awaiting(_exp);
            else return _awaiting.set_reference(*_ret);
        }

    public:

        generator get_return_object() {
            return generator(this);
        }

        promise_type() {}
        ~promise_type() {}

        static constexpr std::suspend_always initial_suspend() noexcept {return {};}


        struct yield_suspend: std::suspend_always {
            promise_type *p = nullptr;
            template<typename X>
            std::coroutine_handle<> await_suspend(std::coroutine_handle<X> me) noexcept {
                p = &me.promise();
                p->_arg = nullptr;
                awaiter *caller = std::exchange(p->_caller, nullptr);
                return caller->resume().pop();
            }
            reference_Arg  await_resume() noexcept {
                if constexpr(!arg_is_void) {
                    return *p->_arg;
                }
            }
        };

        struct yield_null: std::suspend_never { // @suppress("Miss copy constructor or assignment operator")
            promise_type *_p;
            yield_null(promise_type *p):_p(p) {};
            reference_Arg await_resume() noexcept {
                if constexpr(!arg_is_void) {
                    return *_p->_arg;
                }
            }
        };


        yield_suspend final_suspend() noexcept {
            _ret = nullptr;
            return {};
        }
        void unhandled_exception() {
            _exp = std::current_exception();
        }
        void return_void() {
            _done = true;
        }
        yield_suspend yield_value(Ret &x) {
            _ret = &x;
            return {};
        }
        yield_suspend yield_value(Ret &&x) {
            _ret = &x;
            return {};
        }
        yield_null yield_value(std::nullptr_t) {
            return this;
        }

        //set argument pointer
        void set_arg(param_Arg arg) {
            _arg = &arg;
        }

        //generate next item asynchronously
        /*@param caller - pointer to awaiting awaiter */
        /*@return handle of generator to be resumed */
        std::coroutine_handle<> next_async(awaiter *caller) {
            //check state
            assert("Generator is busy" && _caller == nullptr);
            //store caller - will be resumed upon finish
            _caller = caller;
            //retrieve coroutine handle
            auto h = std::coroutine_handle<promise_type>::from_promise(*this);
            //if done, throw exception
            if (h.done()) throw no_more_values_exception();
            //return handle of generator to be resumed
            return h;
        }


        //generate next item synchronously
        void next_sync() {
            //check whether generator is idle (we can't access busy generator)
            assert("Generator is busy" && _caller == nullptr);
            //resume generator now (_caller == nullptr)
            auto h = std::coroutine_handle<promise_type>::from_promise(*this);
            //if generator is finished, throw exception
            if (h.done()) throw no_more_values_exception();
            //reset blocking flag, it blocks thread if generator is asynchronous
            _block.store(false, std::memory_order::relaxed);
            //set caller for later resumption - use internal awaiter
            _caller = &_internal;
            //setup internal awaiter
            _internal.set_resume_fn(resume_fn_sync, this);
            //resume generator, function exits on co_yield or co_await
            h.resume();
            //block thread if the generator still running
            _block.wait(false, std::memory_order_acquire);
        }

        //generate next item and prepare future
        future<Ret> next_future() {
            //check whether generator is idle (we can't access busy generator)
            assert("Generator is busy" && _caller == nullptr);
            //prepare future, retrieve promise
            return [&](auto &&promise) {
                //resume generator now (_caller == nullptr)
                auto h = std::coroutine_handle<promise_type>::from_promise(*this);
                //if generator is finished, throw exception
                if (h.done()) throw no_more_values_exception();
                //setup awaiting promise
                _awaiting = std::move(promise);
                //set caller to internal awaiter
                _caller = &_internal;
                //setup internal awaiter
                _internal.set_resume_fn(resume_fn_future, this);
                //resume generator
                h.resume();
                //once generator is done, _awaiting is set
            };
        }


        bool done() const {
            return _done;
        }

        const std::exception_ptr &exception() const {
            return _exp;
        }

        Ret *value() {
            return _ret;
        }

    };

    generator() = default;
    generator(promise_type *p):_promise(p) {}

    ///This type is returned as result of next();
    /**
     * The function next doesn't perform next step, it only returns this object.
     * You need to test result to bool() and for the first access, it actually
     * calls the generator. The reason for this is that you actually can co_await
     * the result, which performs asynchronous access to the generator. Result
     * of co_await is true or false depend on whether generator generated a new value
     */
    class [[nodiscard]] next_awt: public awaiter {
    public:
        ///constructor - better call generator<>::next()
        next_awt(generator &owner):_owner(owner) {}

        ///if the state of generator is not known, generates next item and returns it state
        /**
         * @retval true next item or exception is available
         * @retval false next item is not avaiable
         */
        operator bool() const {
            if (_state) return true;
            if (this->_owner._promise->done()) return false;
            this->_owner._promise->next_sync();
            return await_resume();
        }

        ///if the state of generator is not known, generates next item and returns it state
        /**
         * @retval false next item is or exception available
         * @retval true next item is not avaiable
         */
        bool operator!() const {
            return !operator bool();

        }

        ///for co_await, determines, whether generator is done
        bool await_ready() const {
            return this->_owner._promise->done();
        }

        ///for co_await suspends coroutine and let the generator to generate next item
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
            set_handle(h);
            return this->_owner._promise->next_async(this);
        }

        ///after generator is finished, retrieves status
        /**
         * @retval true next item or exception is available
         * @retval false next item is not avaiable
         */
        bool await_resume() const {
            _state = !this->_owner._promise->done();
            return _state;
        }

        ///subscribe other awaiter)
        bool subscribe_awaiter(awaiter *awt) {
            this->_owner._promise->next_async(awt).resume();
            return true;
        }


    protected:
        generator &_owner;
        mutable bool _state = false;
    };

    ///Retrieve ID of this coroutine
    coro_id get_id() {
        return std::coroutine_handle<promise_type>::from_promise(*_promise).address();
    }
    ///Returns iterator
    /**
     * Despite on name, this function loads the first item and returns iterator. Further calls
     * loads more and more items Actually there is no separate iterators. The iterator only
     * allows to iterate generated items through the ranged-for (simulation)
     *
     */
    iterator begin() {
        return iterator(*this, next());
    }
    ///Returns iterator which represents end of iteration
    iterator end() {
        return iterator(*this, false);
    }
    ///Runs generator and waits to generation of next item
    /**
     * @return co_awaitable object. If called from non-coroutine, you need to convert returned
     * object to bool to perform loading of next item and returning true/false whether the
     * next item is available
     *
     * @code
     * if (generator.next()) {
     *      //next item is loaded
     * } else {
     *      //generator is done
     * }
     * @endcode
     *
     * @code
     * if (co_await generator.next()) {
     *      //next item is loaded
     * } else {
     *      //generator is done
     * }
     * @endcode
     *
     * @param args the function requires zero on one argument, depend on
     * whethe Arg is void or not. If you pass rvalue reference ensure, that result
     * of this function is immediately used, otherwise, the passed value is destroyed
     * without reaching the generator itself - as it is carried as reference
     *
     *
     */

    template<typename ... Args>
    next_awt next(Args && ... args) {
        if constexpr(arg_is_void) {
            static_assert(sizeof...(args) == 0, "The generator doesn't expect an argument");
            return next_awt(*this);
        } else {
            static_assert(sizeof...(args) == 1, "The generator expects 1 argument");
            _promise->set_arg(args...);
            return next_awt(*this);
        }
    }

    ///Retrieves current value
    /**
     * @return current value
     *
     * @note you need to call next() or check for done()
     */
    Ret &value() {
        auto exp = _promise->exception();
        if (exp) [[unlikely]] std::rethrow_exception(exp);
        auto ret = _promise->value();
        if (ret) [[likely]] return *ret;
        throw value_not_ready_exception();
    }


    ///Allow generator to be called
    /**
     * The generator can be called with zero or one argument depend on whether generator
     * requires argument or not.
     *
     * Result of call is future<Ret>. For generator with infinity cycle, you can
     * co_await the result or use .wait() to access value directly. If the
     * generator is limited, you receive the exception value_not_ready_exception()
     * after last item is generated. However you can co_await future::has_value() and then
     * convert future to bool to receive information whether the value is set. In case
     * that true is returned, you can use dereference (*) to access the result

     *
     * @param args argument of the generator (if enabled)
     * @return future with result
     *
     * @code
     * future<int> val = int_gen();
     * if (co_await val.has_value())
     *          std::cout << *val << std::endl;
     * } else {
     *          std::cout << "Done" << std::endl;
     * }
     * @endcode
     *
     * @b Tip - you can replace existing future with result of generator, using
     *    future<T>::result_of(generator)
     */
    template<typename ... Args>
    future<Ret> operator()(Args && ... args) {
        if constexpr(arg_is_void) {
            static_assert(sizeof...(args) == 0, "The generator doesn't expect an argument");
            return _promise->next_future();
        } else {
            static_assert(sizeof...(args) == 1, "The generator expects 1 argument");
            _promise->set_arg(args...);
            return _promise->next_future();
        }
    }

    ///returns true, if the generator is finished
    bool done() const {
        return _promise->done();
    }

protected:

    struct deleter {
        void operator()(promise_type *p) const noexcept {
            std::coroutine_handle<promise_type>::from_promise(*p).destroy();
        }
    };

    std::unique_ptr<promise_type, deleter> _promise;

};

}

#endif /* SRC_cocls_GENERATOR2_H_ */
