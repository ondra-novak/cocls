#include "check.h"
#include <cocls/generator.h>
#include <iostream>

cocls::generator<int> co_fib2(int count) {
    int a = 0;
    int b = 1;
    for(int i = 0;i < count; i++) {
        int c = a+b;
        co_yield c;
        a = b;
        b = c;
    }
}


int main(int , char **) {

    int results[] = {1,2,3,5,8,13,21,34,55,89,0,0,0};
    auto chk = std::begin(results);
    auto fib2 = co_fib2(10);
    for (int &i: fib2) {
        CHECK_EQUAL(i,*chk);
        chk++;
    }

    return 0;
}
