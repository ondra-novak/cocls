#include "check.h"
#include <cocls/generator.h>

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
}


int main(int, char **) {

    int results[] = {1,2,3,5,8,13,21,34,55,89,0,0,0};
    auto chk = std::begin(results);
    auto gen = co_fib();
    for (int i = 0; i < 10; i++) {
        auto val = gen();
        CHECK_EQUAL(*val,*chk);
        chk++;
    }

}

