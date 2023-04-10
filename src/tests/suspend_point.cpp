#include "check.h"
#include <cocls/future.h>
#include <cocls/coro_queue.h>

cocls::async<void> coro_test(int &counter) {
    counter++;
    co_return;
}

void run_sp(cocls::suspend_point<void> sp) {
    sp.clear();
}

int main() {
    int counter = 0;
    cocls::suspend_point<void> sp1;
    for (int i = 0; i < 10; i++) {
        sp1<<coro_test(counter).detach();
    }
    run_sp(std::move(sp1));
    CHECK_EQUAL(counter, 10);

    for (int i = 0; i < 2; i++) {
        sp1<<coro_test(counter).detach();
    }
    run_sp(std::move(sp1));
    CHECK_EQUAL(counter, 12);


    return 0;
}
