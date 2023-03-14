
#include <iostream>
#include <cocls/future.h>
#include <cocls/shared_future.h>
#include <cocls/thread_pool.h>
#include <cocls/resume.h>

#include <memory>



cocls::async<void> print_thread_task(int i, cocls::future<void> &stopper) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100*i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;
    co_await cocls::parallel(stopper);
    std::this_thread::sleep_for(std::chrono::milliseconds(100*i));
    std::cout << "Task "<< i << " thread " << std::this_thread::get_id() << std::endl;
    co_return;
}



int main(int, char **) {
    std::vector<cocls::shared_future<void> > tasks;
    cocls::future<void> stopper;
    cocls::promise<void> starter = stopper.get_promise();

    for (int i = 0; i < 8; i++) {
        auto t = print_thread_task(i, stopper); 
        tasks.push_back(t);
    }
    starter();
    
    for (auto &t: tasks) {
        t.join();
    }
   
    
}
