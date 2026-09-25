cmake_minimum_required(VERSION 3.21)
project(BatteryProbe LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Static CRT for standalone deployment
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_executable(BatteryProbe BatteryProbe.cpp)
target_link_libraries(BatteryProbe PRIVATE setupapi.lib hid.lib)