#include <iostream>
#include <cocls/thread_pool.h>
#include <cocls/scheduler.h>


cocls::future<void> cancelable(cocls::scheduler &sch, cocls::scheduler::ident id) {
    std::cout << "Hit ENTER to cancel timer (10sec)" << std::endl;
    try {
        co_await sch.sleep_for(std::chrono::seconds(10), id);
        std::cout << "Finished!" << std::endl;
    } catch (const cocls::await_canceled_exception &) {
        std::cout << "Canceled!" << std::endl;
    }
    co_return;
}


int main(int, char **) {
    cocls::thread_pool pool(1);
    cocls::scheduler sch(pool);

    int id;
    auto t = cancelable(sch, &id);
    std::cin.get();
    sch.cancel(&id);
    t.join();
    return 0;
}
