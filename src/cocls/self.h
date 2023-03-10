#pragma once
#ifndef SRC_COCLS_SELF_H_
#define SRC_COCLS_SELF_H_

#include "common.h"

namespace cocls {

///Retrieves handle to currently running coroutine
/**
 * @code
 * std::coroutine_handle<> my_handle = co_await self();
 * @endcode
 */
class self {
public:
    static bool await_ready() noexcept {return false;}
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        _h = h;
        return false;
    }
    std::coroutine_handle<> await_resume() {
        return _h;
    }

protected:
    std::coroutine_handle<> _h;
};


}


#endif /* SRC_COCLS_SELF_H_ */
