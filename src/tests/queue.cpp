#include <iostream>
#include <cocls/future.h>
#include <cocls/queue.h>
#include <cocls/thread_pool.h>


cocls::future<void> queue_task(cocls::queue<int> &q) {
        int i = co_await q.pop();
        while (i) {
            std::cout<<"Received from queue: " << i << std::endl;
            i = co_await q.pop();
        }
        std::cout<<"Done" << std::endl;        
}

void queue_test() {
    cocls::queue<int> q;
    auto task = queue_task(q);
    q.push(1);
    q.push(2);
    q.push(3);
    q.push(0);
    task.join();    
}


int main(int, char **) {
    queue_test();
     
     return 0;
}
