cmake_minimum_required(VERSION 3.10)

# disable testsuite if bundled
if(NOT DEFINED PROJECT_NAME)
  set(NOT_SUBPROJECT ON)
endif()

# Disable building in SRC
if (CMAKE_BINARY_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(FATAL_ERROR "Building in-source is not supported! Create a build dir and remove ${CMAKE_SOURCE_DIR}/CMakeCache.txt")
endif()

project(multi LANGUAGES CXX VERSION 0.0.1)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

# Options
option(MULTI_BUILD_TESTING "Build UnitTesting project" ${NOT_SUBPROJECT})
option(MULTI_BUILD_BENCHMARK "Build Benchmark project" ${NOT_SUBPROJECT})

# Static Library Target
find_package(Threads REQUIRED)
add_library(${PROJECT_NAME} STATIC 
    # Headers
    include/multi/details/jobcontext.inl
    include/multi/details/jobnode.h
    include/multi/details/meta.h
    include/multi/details/nulllock.h
    include/multi/details/queue.h
    include/multi/details/threadpool.h
    include/multi/context.h
    include/multi/handle.h
    include/multi/job.h
    include/multi/jobcontext.h
    include/multi/multi.h
    include/multi/order.h
    include/multi/task.h

    # Source
    src/context.cpp
    src/jobcontext.cpp
    src/jobnode.cpp
    src/multi.cpp
    src/threadpool.cpp
)
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)
if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    target_compile_options(${PROJECT_NAME} PRIVATE /WX /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Werror -Wall -Wextra)
endif()

# Testing Target
if(MULTI_BUILD_TESTING)
    include(addcatch2)
    enable_testing()
    add_executable(test-${PROJECT_NAME}
        test/main.cpp
        test/context.cpp
        test/jobcontext.cpp
        test/jobnode.cpp
        test/queue.cpp
        test/threadpool.cpp
    )
    target_link_libraries(test-${PROJECT_NAME} ${PROJECT_NAME} Catch2::Catch2)
    catch_discover_tests(test-${PROJECT_NAME} TEST_PREFIX "(${PROJECT_NAME}) ")
endif()

# Benchmark Target
if(MULTI_BUILD_BENCHMARK)
    add_executable(bench-${PROJECT_NAME} benchmark/main.cpp )
    target_link_libraries(bench-${PROJECT_NAME} ${PROJECT_NAME})
endif()
