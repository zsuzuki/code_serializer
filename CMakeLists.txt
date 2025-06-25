#
# copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
#
cmake_minimum_required(VERSION 3.31)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(code_serializer)

include_directories(include)

set(src
    src/record.cpp
    src/main.cpp
)

add_executable(${PROJECT_NAME} ${src})

#
