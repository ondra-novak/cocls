#include <iostream>
#include <thread>
#include <cocls/future.h>
#include <cocls/async.h>


cocls::async<void> cofn2(cocls:: promise<int> p) {
    std::cout << "Cofn2 running" << std::endl;
    cocls::pause();
    std::cout << "Switching to promise owner" << std::endl;
    co_await p(42);
    std::cout << "Cofn2 is finishing" << std::endl;

}


cocls::future<int> cofn1() {
    cocls::future<int> f;
    std::cout << "starting cofn2" << std::endl;
    cofn2(f.get_promise()).detach();
    std::cout << "Cofn1 waiting on future" << std::endl;
    int val = co_await f;
    std::cout << "Cofn1 have value " << val << std::endl;
    co_return val;

}

int main(int, char **) {
    std::cout << "Result:" << cofn1().join() <<std::endl;
}
