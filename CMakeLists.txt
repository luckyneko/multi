cmake_minimum_required(VERSION 3.15...3.28)

# disable testsuite if bundled
if(NOT DEFINED PROJECT_NAME)
  set(NOT_SUBPROJECT ON)
endif()

# Disable building in SRC
if (CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

# Default build release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose Release or Debug" FORCE)
endif()

project(multi LANGUAGES CXX VERSION 0.1.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Options
option(MULTI_BUILD_TESTING "Build UnitTesting project" ${NOT_SUBPROJECT})
option(MULTI_BUILD_BENCHMARK "Build Benchmark project" ${NOT_SUBPROJECT})

# Load Catch2
if(MULTI_BUILD_TESTING OR MULTI_BUILD_BENCHMARK)
    include(addcatch2)
endif()

# Static Library Target
find_package(Threads REQUIRED)
add_library(${PROJECT_NAME} STATIC 
    # Headers
    include/multi/details/chaselevdeque.h
    include/multi/details/constants.h
    include/multi/details/context.inl
    include/multi/details/mpmcqueue.h
    include/multi/details/task.h
    include/multi/details/workstealdeque.h
    include/multi/details/workerpool.h
    include/multi/context.h
    include/multi/handle.h
    include/multi/multi.h

    # Source
    src/context.cpp
    src/handle.cpp
    src/multi.cpp
    src/workstealdeque.cpp
    src/workerpool.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)
if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    # /wd4324: cache-line alignas() on small members intentionally pads the struct.
    target_compile_options(${PROJECT_NAME} PRIVATE /WX /W4 /wd4324)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Werror -Wall -Wextra)
endif()

# Testing Target
if(MULTI_BUILD_TESTING)
    enable_testing()
    add_executable(test-${PROJECT_NAME}
        test/multi.cpp
        test/chaselevdeque.cpp
        test/context.cpp
        test/mpmcqueue.cpp
        test/workstealdeque.cpp
        test/workerpool.cpp
    )
    target_link_libraries(test-${PROJECT_NAME} ${PROJECT_NAME} Catch2::Catch2WithMain)
    catch_discover_tests(test-${PROJECT_NAME} TEST_PREFIX "(${PROJECT_NAME}) ")
endif()

# Benchmark Target
if(MULTI_BUILD_BENCHMARK)
    add_executable(bench-${PROJECT_NAME}
        bench/main.cpp
        bench/workloads.cpp
    )
    target_link_libraries(bench-${PROJECT_NAME} ${PROJECT_NAME} Catch2::Catch2)
endif()
