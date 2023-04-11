#include <cocls/generator.h>
#include <cocls/scheduler.h>
#include <cocls/generator_aggregator.h>

#include <iostream>
#include "check.h"

cocls::generator<int> co_fib(cocls::scheduler &sch, int delay, void *ident) {
    int a = 0;
    int b = 1;
    for(;;) {
        int c = a+b;
        co_await sch.sleep_for(std::chrono::milliseconds(delay), ident);
        co_yield c;
        a = b;
        b = c;
    }
}

cocls::future<void> run_test(cocls::scheduler &sch) {
    std::vector<cocls::generator<int> > gens;
    int ident_a, ident_b, ident_c;
    gens.push_back(co_fib(sch,21, &ident_a));
    gens.push_back(co_fib(sch,41, &ident_b));
    gens.push_back(co_fib(sch,91, &ident_c));
    auto gen = cocls::generator_aggregator(std::move(gens));
    int results[] = {1,1,2,3,2,5,1,8,3,13,21,5,34,2,55,8,
                    89,144,13,233,3,377,21,610,987,34,1597,
                    2584,5,55,4181,6765,89,10946,17711,144,8,28657,46368,233};

    for (int i = 0; i < 40; i++) {
        int x = co_await gen();
        CHECK_EQUAL(results[i], x);        
    }

    //cancel all scheduled events - otherwise deadlock
    cocls::suspend_point<void> sp;
    sp <<  sch.cancel(&ident_a) << sch.cancel(&ident_b) << sch.cancel(&ident_c);
    co_await sp;
}

int main(int, char **) {
/*
    std::thread timeout([]{
        std::this_thread::sleep_for(std::chrono::seconds(5));
        std::cerr << "Timeout!" << std::endl;
        abort();
    });    */
    cocls::scheduler sch;
    sch.start(run_test(sch));

}
