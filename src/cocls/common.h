#pragma once
#ifndef SRC_cocls_COMMON_H_
#define SRC_cocls_COMMON_H_

#include <algorithm>
#include <concepts>
#include <coroutine>


#ifdef __CDT_PARSER__
//This part is seen by Eclipse CDT Parser only
//eclipse doesn't support co_await and co_return, so let define some macros

//rewrite co_await to ~, which correctly handles operator co_await -> operator~ and co_await <expr> -> ~ <expr>
#define co_await ~
//rewrite co_return as throw
#define co_return throw
#define co_yield
#define consteval constexpr
#endif

//for compiler support concepts but not for CDT, which doesn't support the keyword 'requires'
#if defined( __cpp_concepts) and not defined (__CDT_PARSER__)
#define CXX20_REQUIRES(...) requires __VA_ARGS__
#else
#define CXX20_REQUIRES(...)
#endif



///Defines multiplier for all statically allocated storages for coroutines
/**
 * In current C++ version (C++20) it is very difficult to determine space needed to
 * coroutine frame. The value must be determined by guessing, trial and error. The
 * final value can be valid for some compilers. For other compilers, the value can
 * be insufficient which results to assert (in debug) or not using static storage at all.
 * If this happen, you can redefine COCLS_STATIC_STORAGE_MULTIPLIER and specify how much this
 * number must be increased globally. The value is in percent, so setting 150 means, that all
 * sizes are multiplied by 1.5 times.
 *
 * This constant can be passed at command line as -DCOCLS_STATIC_STORAGE_MULTIPLIER=150
 */
#ifndef COCLS_STATIC_STORAGE_MULTIPLIER
#ifdef _WIN32
#define COCLS_STATIC_STORAGE_MULTIPLIER 250
#else
#define COCLS_STATIC_STORAGE_MULTIPLIER 100
#endif
#endif

///Coroutine classes use this namespace
namespace cocls {

    ///Coroutine identifier.
    /**
     * For various purposes. For example scheduler<> uses it to cancel sleeping on
     * specified coroutine.
     */
    using coro_id = const void *;



    #if defined( __cpp_concepts) and not defined (__CDT_PARSER__)

    template<typename T>
    concept Storage = requires(T v) {
        {v.alloc(std::declval<std::size_t>())}->std::same_as<void *>;
        {T::dealloc(std::declval<void *>(), std::declval<std::size_t>())}->std::same_as<void>;
    };



    #endif
}


#endif /* SRC_cocls_COMMON_H_ */
