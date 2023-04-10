/**
 * @file detached_thread_pool.cpp
 *
 * demonstration of using detached coroutine with initialize_policy which
 * is needed to run coroutine inside of thread poll
 */


#include "check.h"
#include <cocls/thread_pool.h>
#include <cocls/scheduler.h>
#include <cocls/future.h>
#include <iostream>
#include <memory>


int var = 0;
int var2 = 1;

cocls::async<void> coro_test(cocls::scheduler &sch, cocls::promise<int> prom) {
    var = 1;
    var2 *= 2;
    co_await sch.sleep_for(std::chrono::milliseconds(100));
    var |=2;
    var2 *= 2;
    co_await sch.sleep_for(std::chrono::milliseconds(100));
    var |=4;
    var2 *= 2;
    prom.set_value(42);
}

cocls::async<void> coro_test_2(cocls::scheduler &sch) {
    co_await sch.sleep_for(std::chrono::milliseconds(150));
    var |=8;
    var2 += 1;
}


int main(int, char **) {
    cocls::thread_pool pool(4);
    cocls::scheduler sch(pool);    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::thread timeout([]{
        std::this_thread::sleep_for(std::chrono::seconds(1));
        std::cerr << "Timeout!" << std::endl;
        abort();
    });
    timeout.detach();

    auto t1 = std::chrono::system_clock::now();


    cocls::future<int> fut;
    auto coro = coro_test(sch, fut.get_promise());
    coro_test_2(sch).detach();
    pool.resume(coro.detach());
    int result = fut.wait();
    CHECK_EQUAL(result,42);
    CHECK_EQUAL(var,15);
    CHECK_EQUAL(var2,10);
    auto t2 = std::chrono::system_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(t2-t1).count();
    CHECK_BETWEEN(100,dur,300);


}
