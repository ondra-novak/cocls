#include <iostream>
#include <cocls/future.h>
#include <cocls/async.h>

cocls::async<void> test_task(int id) {
    for (int j = 0; j < 10; j++) {
        for (int i = 0; i < id; i++) std::cout << "\t";
        std::cout << j << std::endl;        
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
