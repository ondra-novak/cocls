#include <forward_list>
#include <iostream>
#include <cocls/thread_pool.h>
#include <cocls/mutex.h>
#include <cocls/scheduler.h>
#include <cocls/resume.h>

cocls::async<void> test_task(cocls::scheduler &sch, cocls::mutex &mx, int &shared_var) {
    auto lk = co_await mx.lock();
    std::cout << "Mutex acquired" << std::endl;
    shared_var++;
    std::cout << "Shared var increased under mutex: " << shared_var << std::endl;
    co_await sch.sleep_for(std::chrono::milliseconds(100));
    std::cout << "Mutex released " << std::endl;
    co_return;
}


cocls::future<void> worker(int n, cocls::mutex &mx) {
    std::cout << "worker with async release " << n <<": Acquire mutex" << std::endl;
    auto own = co_await mx.lock();
    std::cout << "worker with async release " << n <<": Have mutex" << std::endl;
    co_await cocls::pause();
    std::cout << "worker with async release " << n <<": Releasing mutex" << std::endl;
    co_await own.release();
    std::cout << "worker with async release " << n <<": Finish work" << std::endl;

}

cocls::future<void> worker_wa(int n, cocls::mutex &mx) {
    std::cout << "worker without async release " << n <<": Acquire mutex" << std::endl;
    auto own = co_await mx.lock();
    std::cout << "worker without async release " << n <<": Have mutex" << std::endl;
    co_await cocls::pause();
    std::cout << "worker without async release " << n <<": Releasing mutex" << std::endl;
    own.release();
    std::cout << "worker without async release " << n <<": Finish work" << std::endl;

}

cocls::future<void> worker_tp(int n, cocls::mutex &mx, cocls::thread_pool &pool) {
    std::cout << "worker with thread_pool release " << n <<": Acquire mutex (thread_id = " << std::this_thread::get_id() << ")" << std::endl;
    auto own = co_await mx.lock();
    std::cout << "worker with thread_pool release " << n <<": Have mutex (thread_id = " << std::this_thread::get_id() << ")" << std::endl;
    co_await cocls::pause();
    std::cout << "worker with thread_pool release " << n <<": Releasing mutex (thread_id = " << std::this_thread::get_id() << ")" << std::endl;
    pool.resume(own.release());
    std::cout << "worker with thread_pool release " << n <<": Finish work (thread_id = " << std::this_thread::get_id() << ")" << std::endl;

}
cocls::future<void> worker_par(int n, cocls::mutex &mx) {
    std::cout << "worker with parallel release " << n <<": Acquire mutex (thread_id = " << std::this_thread::get_id() << ")" << std::endl;
    auto own = co_await mx.lock();
    std::cout << "worker with parallel release " << n <<": Have mutex (thread_id = " << std::this_thread::get_id() << ")" << std::endl;
    co_await cocls::pause();
    std::cout << "worker with parallel release " << n <<": Releasing mutex (thread_id = " << std::this_thread::get_id() << ")" << std::endl;
    parallel_resume(own.release());
    std::cout << "worker with parallel release " << n <<": Finish work (thread_id = " << std::this_thread::get_id() << ")" << std::endl;

}

cocls::future<void> coro_land() {
    cocls::mutex mx;
    auto f1 = worker(1,mx);
    auto f2 = worker(2,mx);
    auto f3 = worker(3,mx);
    co_await f1;
    co_await f2;
    co_await f3;
    auto f4 = worker_wa(4,mx);
    auto f5 = worker_wa(5,mx);
    auto f6 = worker_wa(6,mx);
    co_await f4;
    co_await f5;
    co_await f6;
    cocls::thread_pool thr(4);
    auto f7 = worker_tp(7,mx, thr);
    auto f8 = worker_tp(8,mx, thr);
    auto f9 = worker_tp(9,mx, thr);
    co_await f7;
    co_await f8;
    co_await f9;
    auto f10 = worker_par(10,mx);
    auto f11 = worker_par(11,mx);
    auto f12 = worker_par(12,mx);
    co_await f10;
    co_await f11;
    co_await f12;

}

int main(int, char **) {
    coro_land().join();
    return 0;

}
