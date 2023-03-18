#include <iostream>
#include <cocls/future.h>
#include <cocls/shared_future.h>
#include <cocls/thread_pool.h>

#include <memory>



cocls::async<void> print_thread_task(int i, cocls::future<void> &stopper, cocls::thread_pool &pool) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100*i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;
    co_await pool(stopper);
    std::this_thread::sleep_for(std::chrono::milliseconds(100*i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;
    co_return;
}



int main(int, char **) {
    std::vector<cocls::shared_future<void> > tasks;
    cocls::future<void> stopper;
    cocls::promise<void> starter = stopper.get_promise();
    cocls::thread_pool pool;

    for (int i = 0; i < 8; i++) {
        auto t = print_thread_task(i, stopper, pool); 
        tasks.push_back(t);
    }
    starter();
    
    for (auto &t: tasks) {
        t.join();
    }
   
    
}
