cmake_minimum_required(VERSION 3.0)

project(disposable)

add_executable(disposable main.cpp)

install(TARGETS disposable RUNTIME DESTINATION bin)
