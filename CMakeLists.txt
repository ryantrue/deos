cmake_minimum_required(VERSION 3.20)
project(deos_host LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(deos_core
    core/src/resource.cpp
    core/src/plan.cpp
    core/src/reconciler.cpp
    core/src/event_bus.cpp
    core/src/state.cpp
    core/src/action.cpp
)
target_include_directories(deos_core PUBLIC core/include)
target_compile_options(deos_core PRIVATE -Wall -Wextra -Wpedantic)

option(DEOS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(DEOS_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer for host tests" OFF)

if(DEOS_WARNINGS_AS_ERRORS)
    target_compile_options(deos_core PRIVATE -Werror)
endif()

if(DEOS_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(DEOS_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_compile_options(deos_core PRIVATE ${DEOS_SANITIZER_FLAGS})
    target_link_options(deos_core PUBLIC ${DEOS_SANITIZER_FLAGS})
endif()

add_executable(deos_host_demo examples/host-demo/main.cpp)
target_link_libraries(deos_host_demo PRIVATE deos_core)

add_executable(deos_core_tests tests/core_tests.cpp)
target_link_libraries(deos_core_tests PRIVATE deos_core)

enable_testing()
add_test(NAME deos_core_tests COMMAND deos_core_tests)
