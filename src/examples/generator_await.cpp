#include <cocls/generator.h>
#include <cocls/future.h>
#include <cocls/async.h>

#include <iostream>

cocls::generator<int> co_fib() {
    int a = 0;
    int b = 1;
    for(;;) {
        int c = a+b;        
        co_yield c;
        a = b;
        b = c;
    }
    co_return;
}

cocls::future<void> co_reader(cocls::generator<int> &&gen) {
    for (int i = 0; i < 10; i++) {
        bool b = co_await gen.next();
        if (b) {
            std::cout << gen.value() << std::endl;
        } else {
            std::cout << "Done" << std::endl;
        }
    }    
    co_return;
    
}

int main(int, char **) {

    auto task = co_reader(co_fib());
    task.join();
    return 0;
}

