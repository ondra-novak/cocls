#include <cocls/async.h>
#include <cocls/future.h>
#include "check.h"

int test_var = 0;

cocls::async<int> int_coro(int x) {
    test_var = x;
    co_return x;
}

cocls::async<int> await_coro(int x) {
    co_return co_await int_coro(x);
}

struct destruct {
    void operator()(int *x) {
        test_var = *x;
        delete x;
    }
};

cocls::async<int> int_coro2(std::unique_ptr<int, destruct> x) {
    co_return *x;
};

int main() {
    int_coro(1).detach();
    CHECK(test_var == 1);
    CHECK(int_coro(2).join() == 2);
    CHECK(int_coro(3).start().join() == 3);
    CHECK(await_coro(4).join() == 4);
    cocls::future<int> v;
    int_coro(5).start(v.get_promise());
    CHECK(*v == 5);
    {
        //will not execute
        auto c = int_coro(6);
    }
    CHECK(test_var == 5);
    {
        //test whether destructor of variable will be called
        auto c = int_coro2(std::unique_ptr<int, destruct>(new int(10)));
    }
    CHECK(test_var == 10);
    {
        //test whether destructor of variable will be called
        auto c = int_coro2(std::unique_ptr<int, destruct>(new int(20)));
        c.detach();
    }
    CHECK(test_var == 20);
}
