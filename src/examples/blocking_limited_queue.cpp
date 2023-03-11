/**
 * @file blocking_limited_queue.cpp
 *
 * Demonstration of limited queue where both sides are awaitable.
 * When the queue is full, the producer (pusher) is blocked and waiting
 * until consumer (poper) removes one item, then the awaiting producer
 * is resumed and new item is pushed
 *
 *
 *
 */
#include <cocls/queue.h>
#include <cocls/scheduler.h>
#include <iostream>


static cocls::limited_queue<std::pair<int, int> > queue(10);
static std::atomic<int> _counter = 0;

static void ident(int id) {
    for (int i = 0; i < id; i++) std::cout << "\t";
}

cocls::async<void> producer(cocls::scheduler &sch, int id, int delay, int count) {
    for (int i = 1; i < count; i++) {
        int val=_counter++;
        ident(id);
        std::cout << "A " <<  val << std::endl;
        co_await queue.push(id,val);
        ident(id);
        std::cout << "F " << val << std::endl;
        co_await sch.sleep_for(std::chrono::milliseconds(delay));

    }
}

cocls::async<void> consumer(cocls::scheduler &sch, int id, int delay) {
    try {
        for (;;) {
            ident(id);
            std::cout << "W" << std::endl;
            auto val = co_await queue.pop();
            ident(id);
            std::cout << "P " << val.first << "," << val.second  << std::endl;
            co_await sch.sleep_for(std::chrono::milliseconds(delay));
        }
    } catch (const cocls::await_canceled_exception &) {
        ident(id);
        std::cout << "canceled";
    }
}


int main(int, char **) {

    cocls::thread_pool tpool;
    cocls::scheduler sch(tpool);
    consumer(sch, 3,500).detach();
    consumer(sch, 4,1000).detach();
    consumer(sch, 5,1100).detach();
    auto p1 = producer(sch, 1, 350, 60).start();
    auto p2 = producer(sch, 2, 300, 60).start();

    p1.join();
    p2.join();

    return 0;
}
