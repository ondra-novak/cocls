#include "check.h"
#include <cocls/future.h>
#include <cocls/coro_queue.h>

cocls::async<void> coro_test(int &counter) {
    counter++;
    co_return;
}

void run_sp(cocls::suspend_point<void> &&sp) {
    cocls::suspend_point<void> sp2 (std::move(sp));
    sp.flush();
}

int main() {
    std::cout << sizeof(cocls::suspend_point<void>) << std::endl;
    int counter = 0;
    cocls::suspend_point<void> sp1;
    for (int i = 0; i < 10; i++) {
        sp1.push_back(coro_test(counter).detach());
    }
    run_sp(std::move(sp1));
    CHECK_EQUAL(counter, 10);
    

    return 0;
}