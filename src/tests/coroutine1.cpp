#include <iostream>
#include <cocls/future.h>



cocls::future<void> coroutine(cocls::future<void> &trigger) {
    std::cout << "Coroutine begin" << std::endl;
    co_await trigger;
    std::cout << "Coroutine ends" << std::endl;
}



int main() {
    cocls::future<void> trigger;
    auto promise = trigger.get_promise();
    auto coro = coroutine(trigger);
    std::cout << "activate trigger" << std::endl;
    promise();
    coro.join();
    return 0;
}
