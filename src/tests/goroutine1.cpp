//example inspired by
//https://go.dev/tour/concurrency/1


#include <iostream>
#include <cocls/future.h>
#include <cocls/scheduler.h>


cocls::future<void> say(cocls::scheduler &sch, std::string s) {
    for (int i = 0; i < 5; i++) {
        co_await sch.sleep_for(std::chrono::milliseconds(100));
        std::cout << s << std::endl;
    }
    co_return;

}

int main(int, char **) {
    cocls::scheduler sch;
    auto t1 = say(sch, "hello");
    auto t2 = say(sch, "world");
    sch.start(t1);
    sch.start(t2);
}
