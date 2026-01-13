cmake_minimum_required(VERSION 3.10)
project(GattServer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(sdbus-c++ REQUIRED)

add_executable(GattServer
    src/GattServer.cpp
    src/main.cpp
)

target_include_directories(GattServer PRIVATE src)

target_link_libraries(GattServer PRIVATE SDBusCpp::sdbus-c++)
