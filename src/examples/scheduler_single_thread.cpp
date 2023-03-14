#include <iostream>
#include <cocls/async.h>
#include <cocls/scheduler.h>

cocls::future<void> test_co(cocls::scheduler &sch) {
    std::cout << "test sleeps 500ms" << std::endl;
    co_await sch.sleep_for(std::chrono::milliseconds(500));
    std::cout << "test sleeps 2s"  << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(2));
    std::cout << "done"  << std::endl;
}

cocls::future<int> test_co2(cocls::scheduler &sch) {
    std::cout << "test sleeps 500ms" << std::endl;
    co_await sch.sleep_for(std::chrono::milliseconds(500));
    std::cout << "test sleeps 2s"  << std::endl;
    co_await sch.sleep_for(std::chrono::seconds(2));
    std::cout << "done"  << std::endl;
    co_return 42;
}


int main(int, char **) {
    ///initialize scheduler
    cocls::scheduler sch;
    ///start the task
    auto task = test_co(sch);
    ///run scheduler until task finishes
    sch.start(task);
    
    ///run again to test stack_allocator whether it adatpted
    sch.start(test_co(sch));

    ///try with return value
    int ret = sch.start(test_co2(sch));
    std::cout << "Scheduler return result of coroutine: " << ret << std::endl;

    ///try with return value (again)
    ret = sch.start(test_co2(sch));
    std::cout << "Scheduler return result of coroutine: " << ret << std::endl;


}
