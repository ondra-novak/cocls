#include <iostream>
#include <cocls/future.h>
#include <cocls/async.h>



cocls::async<void> coroutine(cocls::future<void> &trigger) {
    std::cout << "Coroutine begin" << std::endl;
    co_await trigger;
    std::cout << "Coroutine ends" << std::endl;
}



int main() {
    cocls::future<void> trigger;
    auto promise = trigger.get_promise();
    coroutine(trigger).detach();
    std::cout << "activate trigger" << std::endl;
    promise();
    return 0;
}
