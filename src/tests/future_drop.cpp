#include <cocls/future.h>
#include "check.h"

int main() {
    cocls::future<int> f([&](auto){});
    CHECK_EXCEPTION(cocls::await_canceled_exception, f.value());
}
