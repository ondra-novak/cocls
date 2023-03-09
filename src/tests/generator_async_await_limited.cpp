#include <cocls/generator.h>
#include <cocls/future.h>
#include <cocls/thread_pool.h>

#include <iostream>

cocls::generator<int> co_fib(cocls::thread_pool &pool) {
    int a = 0;
    int b = 1;
    for(int i = 0; i < 10; i++) {
        int c = a+b;
        co_yield c;
        co_await pool;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a = b;
        b = c;
    }
}

cocls::future<void> reader(cocls::generator<int> gen) {
    auto fut = gen();
    while (co_await fut.has_value()) {
        std::cout << *fut << std::endl;
        fut.result_of(gen);
    }
}

int main(int, char **) {

    cocls::thread_pool pool(4);
    reader(co_fib(pool)).join();

}

