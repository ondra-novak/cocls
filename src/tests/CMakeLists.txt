cmake_minimum_required(VERSION 3.1)

link_libraries(pthread)

add_executable(compile_test compile_test.cpp)
add_executable(scheduler_single_thread scheduler_single_thread.cpp)


