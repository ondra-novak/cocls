#pragma once

#ifndef _COCLS_FUTURE_CONVERT_H_
#define _COCLS_FUTURE_CONVERT_H_

#include "future.h"

namespace cocls {

template<auto x> class future_conv;

template<typename From, typename To>
class future_conv_promise_base: public awaiter {
public:
    using awaiter::awaiter;

    template<typename Fn>
    CXX20_REQUIRES(ReturnsFuture<Fn, From>)
    future<To> operator<<(Fn &&fn) {
        return [&](promise<To> prom) {
            _prom = std::move(prom);
            _fut << std::forward<Fn>(fn);
            if (!_fut.subscribe(this)) resume();
        };
    }


    class [[nodiscard]] Hlp {
    public:

        template<typename Fn>
        CXX20_REQUIRES(ReturnsFuture<Fn, From>)
        void operator<<(Fn &&xfn) {            
            _owner._fut << std::forward<Fn>(xfn);
            if (!_owner._fut.subscribe(&_owner)) _owner.resume();
        }


    protected:
        Hlp(future_conv_promise_base &owner):_owner(owner) {}
        future_conv_promise_base &_owner;
        friend class future_conv_promise_base;
    };

    Hlp operator()(promise<To> &&prom) {
        _prom = std::move(prom);
        return Hlp(*this);
    }

protected:
    promise<To> _prom;
    future<From> _fut;
};


template<typename From, typename To, typename Context, To (Context::*fn)(From &)> 
class future_conv<fn>: public future_conv_promise_base<From, To> {
public:
    future_conv(Context *ctx):future_conv_promise_base<From,To>([](awaiter *me, void *user_ctx) noexcept -> suspend_point<void> {
        Context *ctx = reinterpret_cast<Context *>(user_ctx);
        future_conv *_this = static_cast<future_conv *>(me);
        promise<To> p = std::move(_this->_prom);
        try {
            return p((ctx->*fn)(*_this->_fut));
        } catch (...) {
            return p(std::current_exception());
        }
    }, ctx) {}
};

template<typename From, typename To, typename Context, suspend_point<void> (Context::*fn)(From &, promise<To> &)> 
class future_conv<fn>: public future_conv_promise_base<From, To>{
public:
    future_conv(Context *ctx):future_conv_promise_base<From, To>([](awaiter *me, void *user_ctx) noexcept -> suspend_point<void> {
        Context *ctx = reinterpret_cast<Context *>(user_ctx);
        future_conv *_this = static_cast<future_conv *>(me);
        promise<To> p = std::move(_this->_prom);
        try {
            return (ctx->*fn)(*_this->_fut, p);
        } catch (...) {
            return p(std::current_exception());
        }
    }, ctx) {}
};
template<typename From, typename To, To (*fn)(From &)> 
class future_conv<fn>: public future_conv_promise_base<From, To> {
public:
    future_conv():future_conv_promise_base<From, To>([](awaiter *me, void *) noexcept -> suspend_point<void> {
        future_conv *_this = static_cast<future_conv *>(me);
        promise<To> p = std::move(_this->_prom);
        try {
            return p(fn(*_this->_fut));
        } catch (...) {
            return p(std::current_exception());
        }
    }, nullptr) {}
};

template<typename From, typename To, typename Context, To (*fn)(From &, Context *)> 
class future_conv<fn>: public future_conv_promise_base<From, To> {
public:
    future_conv(Context *ctx):future_conv_promise_base<From, To>([](awaiter *me, void *user_ctx) noexcept -> suspend_point<void> {
        Context *ctx = reinterpret_cast<Context *>(user_ctx);
        future_conv *_this = static_cast<future_conv *>(me);
        promise<To> p = std::move(_this->_prom);
        try {
            return p(fn(*_this->_fut, ctx));
        } catch (...) {
            return p(std::current_exception());
        }
    }, ctx) {}
};

}


#endif
