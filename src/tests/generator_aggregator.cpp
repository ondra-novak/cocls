#include <cocls/generator_aggregator.h>

#include <cocls/generator.h>
#include "check.h"

#include <iostream>

cocls::generator<int> co_fib(int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i<count;i++) {
        int c = a+b;
        co_yield c;
        a = b;
        b = c;
    }
}


int main(int, char **) {

    std::vector<cocls::generator<int> > gens;
    gens.push_back(co_fib(5));
    gens.push_back(co_fib(10));
    gens.push_back(co_fib(15));
    auto gen = cocls::generator_aggregator(std::move(gens));
    int results[] = {1,1,1,2,2,2,3,3,3,5,5,5,8,8,8,13,13,21,21,34,34,55,55,89,89,144,233,377,610,987};
    int pos = 0;
    for(;;) {
        auto val = gen();
        if (val) {
            CHECK_EQUAL(*val, results[pos]);
            ++pos;
        } else {
            break;
        }

    }
}

