#include "check.h"

#include <cocls/mutex.h>
#include <cocls/future.h>
#include <cocls/async.h>
#include <thread>


static std::vector<int> results;


void run_in_thread(cocls::suspend_point<void> &&pt, std::atomic<bool> &start) {
    std::thread thr([pt = std::move(pt), &start]()mutable{
        start.wait(false);
        pt.clear();
    });
    thr.detach();
}

cocls::async<void> coro_test1(cocls::mutex &mx, int id) {
    cocls::mutex::ownership own = co_await mx.lock();    
    results.push_back(id);
    own.release();
}

cocls::async<void> coro_test2(cocls::mutex &mx, int id) {
    cocls::mutex::ownership own = co_await mx.lock();
    results.push_back(id);
    co_await own.release();
}

cocls::future<void> coro_test(cocls::mutex &mx, int id) {
    cocls::mutex::ownership own = co_await mx.lock();
    results.push_back(id);
    std::atomic<bool> start(false);
    cocls::future<void> f1;
    cocls::future<void> f2;
    cocls::future<void> f3;
    cocls::future<void> f4;
    run_in_thread(coro_test1(mx, id+1).start(f1.get_promise()), start);
    run_in_thread(coro_test2(mx, id+2).start(f2.get_promise()), start);
    run_in_thread(coro_test1(mx, id+3).start(f3.get_promise()), start);
    run_in_thread(coro_test2(mx, id+4).start(f4.get_promise()), start);
    start.store(true);
    start.notify_all();
    co_await own.release();
    co_await f1;
    co_await f2;
    co_await f3;
    co_await f4;
}


int main() {
    cocls::mutex mx;
    //just crash test (should not crash if lock works)
    for (int i = 0; i< 100; i++) {
        coro_test(mx,i*10).join();

    }    
 
}