#include <cocls/signal.h>
#include <cocls/async.h>
#include <iostream>
#include <cocls/future.h>
#include <thread>

cocls::async<void> listener(int id, cocls::signal<int>::emitter awt, bool forever) {
    try {
        do {
            int i = co_await awt;
            std::cout << "Listener " << id << " received: " << i << std::endl;
        } while (forever);
    } catch (const cocls::await_canceled_exception &) {
        std::cout << "Listener " << id << " done" << std::endl;
    }
}


cocls::async<void> void_listener(int id, cocls::signal<void>::emitter awt, bool forever) {
    try {
        do {
            co_await awt;
            std::cout << "Listener " << id << " received: void " << std::endl;
        } while (forever);
    } catch (const cocls::await_canceled_exception &) {
        std::cout << "Listener " << id << " done" << std::endl;
    }
}

bool callback_listener(int val) {
    std::cout << "Callback listener: " << val << std::endl;
    return true;
}

static void signal_generator(std::function<void(int) > sig) {
    std::thread thr([sig=std::move(sig)]{
        sig(10);
        sig(20);
        sig(30);
        sig(40);
    });
    thr.detach();
}

cocls::async<void> signal_as_fn() {

    try {
        auto awt = cocls::signal<int>::hook_up(signal_generator);
        do {
            int i = co_await awt;
            std::cout<<"Signal as fn - next value: " << i << std::endl;
        } while(true);
    } catch (const cocls::await_canceled_exception &) {
        std::cout<<"Signal as fn: done" << std::endl;
    }

}


int main() {


    {
        cocls::signal<int> slot;
        listener(1,slot.get_emitter(), true).detach();
        listener(2,slot.get_emitter(), true).detach();
        listener(3,slot.get_emitter(), true).detach();
        slot.connect(callback_listener);

        auto rcv = slot.get_collector();

        rcv(10);
        std::cout << "---------------" << std::endl;
        rcv(20);
        std::cout << "---------------" << std::endl;
        listener(4,slot.get_emitter(), false).detach();
        rcv(30);
        std::cout << "---------------" << std::endl;
        rcv(40);
        std::cout << "---------------" << std::endl;
        rcv(50);
        std::cout << "---------------" << std::endl;
    }
    std::cout << "---------------" << std::endl;
    {
        cocls::signal<void> void_slot;
        void_listener(10, void_slot.get_emitter(), true).detach();
        void_slot.connect([](){
            std::cout << "callback void" << std::endl;
            return true;
        });
        auto rcv = void_slot.get_collector();
        rcv();
        std::cout << "---------------" << std::endl;
        rcv();
        std::cout << "---------------" << std::endl;
        rcv();
        std::cout << "---------------" << std::endl;
    }
    std::cout << "---------------" << std::endl;

    signal_as_fn().start().join();

}
