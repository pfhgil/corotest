cmake_minimum_required(VERSION 3.30)
project(corotest2)

set(CMAKE_CXX_STANDARD 26)

add_executable(corotest2 main.cpp coro.h AsyncTimer.h)
