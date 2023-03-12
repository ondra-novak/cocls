#include "check.h"
#include <cocls/queue.h>
#include <cocls/async.h>


cocls::async<void> coro(cocls::queue<int> &q) {
    auto f = q.pop();
    int val;
    CHECK_EQUAL(f.ready() , false);
    val = co_await f;
    CHECK_EQUAL(val,10);
    val = co_await q.pop();
    CHECK_EQUAL(val,20);
    CHECK_EXCEPTION(cocls::await_canceled_exception, val = co_await q.pop());
}


int main() {
    auto q = std::make_unique<cocls::queue<int> >();
    auto f = coro(*q).start();
    q->push(10);
    q->push(20);
    q.reset();
    f.join();

    return 0;

}
