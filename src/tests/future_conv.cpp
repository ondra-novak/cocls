#include "check.h"

#include <cocls/future_conv.h>
#include <cocls/async.h>


#include <iostream>
#include <optional>
#include <thread>

cocls::future<int> work() {
    return [](cocls::promise<int> p){
        std::thread thr([p = std::move(p)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
           p.set_value(42);
        });
        thr.detach();
    };
}



class Conv1 {
public:
    Conv1(int x):convertor(this),_x(x) {}

    int conv_fn(int &src) {
        return src+_x;
    }

    cocls::future_conv<&Conv1::conv_fn> convertor;

protected:
    int _x;

};

class Conv2 {
public:
    Conv2(int x):convertor(this),c(44),_x(x) {}

    cocls::suspend_point<void> conv_fn(int &src, cocls::promise<int> &prom) {        
        if (_tmp_res.has_value()) {
            int t = *_tmp_res;
            _tmp_res.reset();
            return prom(t + src+_x);
        }
        _tmp_res = src;
        convertor(std::move(prom)) << [&]()->cocls::future<int>{
            return c.convertor << [&]()->cocls::future<int>{
                return work();
            };
        };
        return {};

    }

    cocls::future_conv<&Conv2::conv_fn> convertor;

protected:
    Conv1 c;
    std::optional<int> _tmp_res;
    int _x;

};

int conv_fn2(int &x) {
    return x*2;
}


cocls::future<int> cofn1() {
    Conv1 c(22);
    co_return co_await (c.convertor << [&]{return work();});
}

cocls::future<int> cofn2() {
    cocls::future_conv<&conv_fn2> c2;
    co_return co_await (c2 << [&]{return work();});
}
cocls::future<int> cofn3() {
    Conv2 c2(31);
    co_return co_await (c2.convertor << [&]{return work();});
}

int main(int, char **) {
    CHECK_EQUAL(cofn1().wait(), 64);
    CHECK_EQUAL(cofn2().wait(), 84);
    CHECK_EQUAL(cofn3().wait(), 159);


}

