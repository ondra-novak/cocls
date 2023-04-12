#include <iostream>
#include <cocls/future.h>
#include "check.h"

std::vector<int> results = {
    0,10,20,30,40,
    1,11,21,31,41,
    2,12,22,32,42,
    3,13,23,33,43,
    4,14,24,34,44,
};

static auto iter = results.begin();

cocls::async<void> test_task(int id) {
    for (int j = 0; j < 5; j++) {
        int v = id*10+j;
        CHECK_EQUAL(v, *iter);
        ++iter;
        co_await cocls::pause();
    }     
}


cocls::future<void> test_cooperative() {
    //cooperative mode need to be initialized in a coroutine.
    //The cooperative execution starts once coroutine exits
       for (int i = 0; i < 5; i++) {
           test_task(i).detach();
       }
    co_return;
}


int main(int, char **) {
    test_cooperative().join();
    
}
