#pragma once
#ifndef SRC_COCLS_ALLOCA_STORAGE
#define SRC_COCLS_ALLOCA_STORAGE

#include <memory>
#include <utility>

namespace cocls {

/// Creates coroutine frame on stack with help of alloca() function
/**
 * This can replace defunct coroutine allocation elision. To use this class, you
 * need to create a shared state, which consists of single number variable (std::size_t), This
 * variable can be preinitialized with some arbitrary constant, which defines initial size of the
 * frame. If the coroutine fits to preallocated frame, then no allocation is done. If not, then
 * coroutine is allocated on heap and shared state is update to fit next time.
 * 
 * To allocate on stack, you need to use coroutine cocls::with_allocator. Then use following code
 * 
 * @code
 * stack_storage storage(_state); //state is shared state
 * storage = alloca(storage);   //allocate storage on stack
 * run_coro(storage, ...);  //run coroutine allocated on stack,  the coroutine need to use with_allocator
 * @endcode
*/
class stack_storage {
public:

    stack_storage(std::size_t &state):_state(state),_alloc_size(std::max<std::size_t>(0,_state)) {}

    void operator=(void *ptr) {
        _alloc_ptr = ptr;
    }
    operator std::size_t() const {
        return _alloc_size;
    }

    void *alloc(std::size_t sz) {
        if (sz+1 <=_alloc_size) {
            char *flag = reinterpret_cast<char *>(_alloc_ptr)+sz;
            *flag = 0;
            return _alloc_ptr;
        } else {
            void *ptr = ::operator new (sz+1);
            _state = sz+1;
            char *flag = reinterpret_cast<char *>(ptr)+sz;
            *flag = 1;
            return ptr;
        }
    }

    static void dealloc(void *ptr, std::size_t sz) {
        char *flag = reinterpret_cast<char *>(ptr)+sz;
        if (*flag) ::operator delete(ptr);
    }

protected:
    std::size_t &_state;
    std::size_t _alloc_size;
    void *_alloc_ptr;
};



}



#endif