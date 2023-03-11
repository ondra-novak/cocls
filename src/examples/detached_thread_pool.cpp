/**
 * @file detached_thread_pool.cpp
 *
 * demonstration of using detached coroutine with initialize_policy which
 * is needed to run coroutine inside of thread poll
 */


#include <cocls/thread_pool.h>
#include <cocls/scheduler.h>
#include <cocls/future.h>
#include <iostream>
#include <memory>


cocls::async<void> coro_test(cocls::scheduler &sch, cocls::promise<int> prom) {
    std::cout << "coroutine is running" << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(1));
    std::cout << "coroutine is running... 2" << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(1));
    std::cout << "coroutine resolving promise" << std::endl;
    prom.set_value(42);
    std::cout << "coroutine finished" << std::endl;
}


int main(int, char **) {
    auto pool = std::make_shared<cocls::thread_pool> (4);
    cocls::scheduler sch(*pool);

    cocls::future<int> fut;
    auto coro = coro_test(sch, fut.get_promise());
    pool->run_detached(std::move(coro));
    int result = fut.wait();
    std::cout << "Result:" << result << std::endl;


}
