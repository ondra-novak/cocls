#include <iostream>
#include <cocls/signal.h>



cocls::future<void> coroutine(cocls::signal<int>::emitter source) {
    std::cout << "Coroutine1 begin" << std::endl;
    int i = co_await source;
    std::cout << "Coroutine1 received " << i << std::endl;
    int j = co_await source;
    std::cout << "Coroutine1: ... and received " << j << std::endl;
    int k = co_await source;
    std::cout << "Coroutine1: ... and also " << k << std::endl;
    std::cout << "Coroutine1: ending" << std::endl;
}

cocls::future<void> coroutine2(cocls::signal<std::string>::emitter source) {
    std::cout << "Coroutine2 begin" << std::endl;
    auto i = co_await source;
    std::cout << "Coroutine2 received " << i << std::endl;
    auto j = co_await source;
    std::cout << "Coroutine2: ... and received " << j << std::endl;
    auto k = co_await source;
    std::cout << "Coroutine2: ... and also " << k << std::endl;
    std::cout << "Coroutine2: ending" << std::endl;
}



int main() {
    cocls::signal<int> s1;
    cocls::signal<std::string> s2;

    auto f1 = coroutine(s1.get_emitter());
    auto f2 = coroutine2(s2.get_emitter());

    auto col1 = s1.get_collector();
    auto col2 = s2.get_collector();

    col1(10);
    col2("Hello");
    col2("World");
    col1(42);
    col2("Wide");
    col2("not seen");
    col1(50);

    f1.join();
    f2.join();


}
