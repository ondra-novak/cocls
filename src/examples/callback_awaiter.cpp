#include <iostream>
#include <thread>
#include <cocls/callback_awaiter.h>
#include <cocls/future.h>


cocls::future<int> work() {
    return [](cocls::promise<int> p){
        std::thread thr([p = std::move(p)]() mutable {
            std::cout << "In a thread" << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
           p(42);
        });
        thr.detach();
    };
}

cocls::future<int> cofn1() {
    cocls::future<int> fut([](auto promise) {
        cocls::callback_await<cocls::future<int> >([promise = std::move(promise)](cocls::await_result<int> value) mutable {
            try {
                std::cout << "Callback result:" << *value << std::endl;
            } catch (...) {
                promise(std::current_exception());
            }
        },[&]{return work();});
    });

    co_return co_await fut;
}

int main(int, char **) {
    std::cout << "Result:" << cofn1().join() <<std::endl;

}
