cmake_minimum_required(VERSION 3.10)
project(PiGattServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(sdbus-c++ REQUIRED)

add_executable(PiGattServer
    src/GattServer.cpp
    src/main.cpp
)

target_include_directories(PiGattServer PRIVATE src)

target_link_libraries(PiGattServer PRIVATE SDBusCpp::sdbus-c++)
