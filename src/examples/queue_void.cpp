#include <iostream>
#include <cocls/future.h>
#include <cocls/queue.h>
#include <cocls/thread_pool.h>


cocls::async<void> queue_task(cocls::queue<void> &q) {
    try {
        while(true) {
            co_await q.pop();
            std::cout<<"Received event from queue(void) " << std::endl;
        }
    } catch (const cocls::await_canceled_exception &) {
        std::cout<<"Queue destroyed " << std::endl;
    }
 }

cocls::future<void> queue_test() {
    return [](auto promise) {
        cocls::queue<void> q;

        queue_task(q).start(promise);
        q.push();
        q.push();
        q.push();
        q.push();
    };
}


int main(int, char **) {
    queue_test().join();
    return 0;
}
