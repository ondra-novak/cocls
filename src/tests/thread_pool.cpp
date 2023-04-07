#include "check.h"
#include <iostream>
#include <cocls/future.h>
#include <cocls/thread_pool.h>



cocls::future<int> co_test(cocls::thread_pool &pool) {

    std::thread::id id1, id2, id3, id4, id5;
    auto example_fn = [&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        id3 = std::this_thread::get_id();
        return 42;
    };
    id1 = std::this_thread::get_id();
    co_await pool;
    id2 = std::this_thread::get_id();
    auto f = pool.run(example_fn);;
    id4 = std::this_thread::get_id();
    CHECK_EQUAL(id2,id4);
    int r = co_await f;
    CHECK_NOT_EQUAL(id1,id2);
    id5 = std::this_thread::get_id();
    CHECK_EQUAL(id5,id3);
    co_return r;
}

cocls::future<void> co_test2(cocls::promise<int> &p, int w, std::thread::id id, bool eq) {
    cocls::future<int> f;
    p = f.get_promise();
    int v = co_await f;
    CHECK_EQUAL(v,w);
    auto id1 = std::this_thread::get_id();
    if (eq) {
        CHECK_EQUAL(id, id1);
    } else {
        CHECK_NOT_EQUAL(id, id1);
    }
}

cocls::async<std::thread::id> get_id_coro() {
    co_return std::this_thread::get_id();
}

int main(int, char **) {
    cocls::thread_pool pool(5);
    int r = co_test(pool).join();
    CHECK_EQUAL(r,42);

    {
        cocls::promise<int> p;
        auto f = co_test2( p, 12,std::this_thread::get_id(), true);
        p(12);
        f.join();
    }

    {
        cocls::promise<int> p;
        auto f = co_test2( p, 34,std::this_thread::get_id(), false);
        pool.run(p(34));
        f.join();
    }

    {
        auto id1 = pool.run(get_id_coro()).join();
        auto id2 = std::this_thread::get_id();
        CHECK_NOT_EQUAL(id1,id2);

    }




}
