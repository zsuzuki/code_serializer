#
# copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
#
cmake_minimum_required(VERSION 3.31)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(code_serializer)

add_library(${PROJECT_NAME}
    src/record.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC include)

add_executable(${PROJECT_NAME}_example
    examples/main.cpp
)
target_link_libraries(${PROJECT_NAME}_example PRIVATE ${PROJECT_NAME})

enable_testing()
add_executable(${PROJECT_NAME}_test
    tests/main_test.cpp
)
target_link_libraries(${PROJECT_NAME}_test PRIVATE ${PROJECT_NAME})
add_test(NAME ${PROJECT_NAME}_test COMMAND ${PROJECT_NAME}_test)

#
