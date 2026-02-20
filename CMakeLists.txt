#
# copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
#
cmake_minimum_required(VERSION 3.31)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(code_serializer)

add_library(${PROJECT_NAME}_lib
    src/record.cpp
)
target_include_directories(${PROJECT_NAME}_lib PUBLIC include)

add_executable(${PROJECT_NAME}
    src/main.cpp
)
target_link_libraries(${PROJECT_NAME} PRIVATE ${PROJECT_NAME}_lib)

enable_testing()
add_executable(${PROJECT_NAME}_test
    tests/main_test.cpp
)
target_link_libraries(${PROJECT_NAME}_test PRIVATE ${PROJECT_NAME}_lib)
add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)

#
