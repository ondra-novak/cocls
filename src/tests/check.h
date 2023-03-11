#pragma once

#include <iostream>

#define CHECK(x) do { \
    if(!(x)) {  \
        std::cerr << "FAILED: " << #x << std::endl; \
        abort();\
    } else {\
        std::cout << "Passed: " << #x << std::endl;\
    }\
}while(false)


#define CHECK_EQUAL(a,b) do { \
    if((a) == (b)) {  \
        std::cout << "Passed: " << (a) << "==" << (b) << std::endl;\
    } else {\
        std::cerr << "FAILED: " << (a) << "==" << (b) << std::endl; \
        abort();\
    } \
}while(false)


