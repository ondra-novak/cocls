/** @file thread_pool_resumption_policy.cpp
 * 
 * Demonstrates using thread_pool_resumption_policy, which allows to bind a thread pool to a task. The
 * task is then resumed in this thread pool. 
 * 
 * Because there is no way how to initialize policy directly, it must be initialized by a function 
 * initialize_policy. Until this happen, the task is suspended. Once the task has a thread pool associated,
 * then it is started
 * 
 * So the function initialize_policy actually starts the task
 * 
 * Second part stops these tasks on ordinary task, which exits after 2 second, this resumes
 * thread_pool's task, they are still resumed on this thread pool, regardless on that ordinary
 * task was finished on main thread
 * 
 * @note Thread pool must be shared through the std::shared_ptr<> to ensure, that pool
 * exists till there is task which is using it
 * 
 */
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
