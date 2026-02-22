#
# copyright 2025 y.suzuki(wave.suzuki.z@gmail.com)
#
cmake_minimum_required(VERSION 3.31)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(code_serializer)

option(RECORD_FAST_DIFF_COPY "Enable fast (non-atomic) serializeDiffAndCopy path" OFF)

add_library(${PROJECT_NAME}
    src/record.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_compile_definitions(${PROJECT_NAME}
    PUBLIC
      $<$<BOOL:${RECORD_FAST_DIFF_COPY}>:RECORD_FAST_DIFF_COPY>
)

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

add_executable(${PROJECT_NAME}_perf
    tests/perf_bench.cpp
)
target_link_libraries(${PROJECT_NAME}_perf PRIVATE ${PROJECT_NAME})

#
