#pragma once

#ifndef SRC_COCLS_SUSPEND_POINT_H_
#define SRC_COCLS_SUSPEND_POINT_H_

#include "coro_queue.h"

namespace cocls {


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
 * Instance of suspend_point doesn't allocate a memory until count of ready coroutines cross threshold 3.
 */
template<typename RetVal>
class suspend_point;


template<>
class suspend_point<void> {
public:
    static constexpr int inline_count = 3;

    ///construct default empty suspend point
    constexpr suspend_point() = default;
    ///construct suspend point add one coroutine handle
    /**
     * @param h coroutine handle to include
    */
    constexpr suspend_point(std::coroutine_handle<> h):_count_flag(2) {
        _local._handles[0] = h.address();
    }
    ///move suspend point
    /** Suspend point is movable */
    constexpr suspend_point(suspend_point &&other):_count_flag(other._count_flag) {
        if (_count_flag & 1) [[unlikely]] {
            _ext = other._ext;
        } else {
            _local = other._local;
        }
        other._count_flag = 0;
    }

    ///merge coroutines from one suspend point to other (current)
    suspend_point &operator<<(suspend_point &&other)  {
        auto count = other._count_flag >> 1;
        if (other._count_flag & 1) [[unlikely]] {
            for (std::size_t i = 0; i < count; i++) {
                add(other._ext._handles[i]);
            }
            delete [] other._ext._handles;
        } else {
            for (std::size_t i = 0; i < count; i++) {
                add(other._local._handles[i]);
            }
        }
        other._count_flag = 0;
        return *this;
    }

    ///merge coroutines from one suspend point to other (current)
    constexpr suspend_point &operator<<(std::coroutine_handle<> &&other)  {
        add(other.address());
        return *this;
    }


    ///suspend point is not copyable
    suspend_point(const suspend_point &) = delete;
    ///suspend point is not copy assignable
    suspend_point &operator=(const suspend_point &) = delete;
    ///You can assign to suspend point by move, however this merges data (like <<)
    suspend_point &operator=( suspend_point &&other) {return operator<< (std::move(other));}

    ///destructor
    /** Destructor always resumes all remaining coroutines */
    constexpr ~suspend_point() {
        if (_count_flag) suspend_now();
    }

    ///retrieves count of waiting coroutines
    constexpr std::size_t size() const noexcept {return _count_flag >> 1;}

    ///returns true, if suspend_point is empty
    constexpr bool empty() const noexcept {return (_count_flag >> 1) == 0;}

    ///clears suspend point - which causes, that all coroutines are resumed now
    constexpr void clear() {
        suspend_now();
    }

    ///pop one coroutine from the suspend point and return it as result
    /**
     * @return one coroutine handle from the list. The handle is removed from the list. If the
     * object is empty, returns std::noop_coroutine. This function is usedful to implement
     * symmetric transfer in await_suspend(), just return handle retrieved by pop()
    */
    constexpr std::coroutine_handle<> pop() noexcept {
        std::size_t idx = (_count_flag >> 1);
        if (idx) [[likely]] {
            _count_flag-=2;
            void **from = (_count_flag & 0x1)?_ext._handles:_local._handles;
            return std::coroutine_handle<>::from_address(from[idx-1]);
        } else {
            return std::noop_coroutine();
        }
    }

    ///suspend execution now and switch to waiting coroutines
    constexpr void suspend_now() noexcept {
        if (!empty()) {
            if (coro_queue::is_active()) {
                for (auto x: *this) {
                    coro_queue::instance->push(std::coroutine_handle<>::from_address(x));
                }
            } else {
                coro_queue::install_queue_and_call([&]{
                    for (auto x: *this) {
                        std::coroutine_handle<>::from_address(x).resume();
                    }
                });
            }
            clear_internal();
        }
    }

    ///implements co_await's shortcut. If suspend point is empty, no suspend is needed
    constexpr bool await_ready() const noexcept {
        return empty();
    }

    ///impolements co_await resume - it is empty
    static constexpr void await_resume() noexcept {};


    ///Implements co_await's suspend phase
    /**
     * Suspends current coroutine, moves all coroutines from list to the coro_queue,
     * except one, which is returned as result for symmetric transfer. The current
     * coroutine is also put into coro_queue as the last item
     *
     * If the coro_queue is not active, the approach is different. The
     * function activates coro_queue and resumes all coroutines under coro_queue,
     * Execution of coroutines are performed inside if this function. Once
     * all is done, current coroutine is resumed
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
        //ensure that current queue is active
        if (coro_queue::is_active()) [[likely]]  {
            std::coroutine_handle<> out = pop();
            void *me_addr = h.address();
            //check, whether my coroutine handle is also in the list (to avoid double insert)
            bool me_included = false;            
            for (auto x: *this) {
                me_included |= x == me_addr;
                coro_queue::instance->push(std::coroutine_handle<>::from_address(x));
            }
            //if not, include me.
            if (!me_included) {
                coro_queue::instance->push(h);
            }
            clear_internal();
            return out;
        } else {
            //this can happen, when co_await is called outside of coro_queue framework
             coro_queue::install_queue_and_call([&]{
                return await_suspend(h);
             });
            
        }
    }


protected:

    using Ptr = void *;

    struct InlineData {
        Ptr _handles[inline_count];
    };
    struct ExtData {
        Ptr *_handles;
        std::size_t _capacity;
    };

    //for small amount of handles, they are stored in the object
    //for large amount of hansles, heap is used
    //for 3 internal handles + 1 size, total size of the object is 4xsizeof(pointer) = 32 on x64
    union {
        InlineData _local;
        ExtData _ext;
    };

    //contains count+flag, flag is at BIT 0, count starts at BIT 1. You need to shift >> 1 to retrieve count
    unsigned int _count_flag = 0;

    //clear internal state - it is expected, that handles has been resumed
    constexpr void clear_internal() {
        if (_count_flag & 1) [[unlikely]] {
            delete [] _ext._handles;
        }
        _count_flag = 0;
    }

    //add handle (as pointer)
    constexpr void add(Ptr h) {
        //determine, whether we are local or external
        bool flag = (_count_flag & 1) != 0;;
        //retrieve current count
        unsigned int count = _count_flag >> 1;
        //in most cases, flag is set to 0,
        if (flag) [[unlikely]] {
            //check, if we reached capacity
            if (count == _ext._capacity) [[unlikely]] {
                //perform realloc (alloc new array, copy amd dealloc old array)
                Ptr *nh = new Ptr[count * 2];
                std::copy(_ext._handles, _ext._handles+count, nh);
                delete[] _ext._handles;
                //store new array
                _ext._handles = nh;
                //store new capacity
                _ext._capacity = count*2;
            }
            //add new handle to poisition
            _ext._handles[count] = h;
            //increase count (by 2, because counter is shifted)
            _count_flag += 2;
        } else {
            //if we are still below the capacity
            if (count < inline_count)  [[likely]] {
                //just put handle
                _local._handles[count] = h;
                //and increase counter (2 because shifted)
                _count_flag += 2;
            } else {
                //if we reached capacity
                //create array on heap
                Ptr *nh = new Ptr[count * 2];
                //copy content
                std::copy(std::begin(_local._handles), std::end(_local._handles), nh);
                //start using _ext, initialize it
                _ext._handles = nh;
                _ext._capacity = count*2;
                //put handle to position
                _ext._handles[count] = h;
                //increase counter and set flag (1 * 2 + 1 = 3);
                _count_flag += 3;
            }
        }
    }

    constexpr const Ptr *begin() const {
        if (_count_flag & 1) [[unlikely]] return _ext._handles;
        else return _local._handles;
    }

    constexpr const Ptr *end() const {
        return begin() + (_count_flag>>1);
    }

};


template<typename X>
class suspend_point: public suspend_point<void> {
public:
    ///Constructs empty suspend point, but set its value
    suspend_point(X val):value(std::move(val)) {}
    ///Constructs empty suspend point, but set its value
    suspend_point(std::coroutine_handle<> h, X val)
        :suspend_point<void>(h),value(std::move(val)) {}
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
                ss << instance->_queue.back();
                instance->_queue.pop_back();
            }
            return ss;
        } else {
            ret_v v = fn();
            while (instance->_queue.size() > sz) {
                ss << instance->_queue.back();
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

#endif
