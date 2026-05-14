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

include(GNUInstallDirs)

# Options
option(MULTI_BUILD_TESTING "Build UnitTesting project" ${NOT_SUBPROJECT})
option(MULTI_BUILD_BENCHMARK "Build Benchmark project" ${NOT_SUBPROJECT})
option(MULTI_BUILD_EXAMPLES "Build example programs" ${NOT_SUBPROJECT})
option(MULTI_INSTALL "Generate install/export rules for multi" ${NOT_SUBPROJECT})

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
    include/multi/details/job.h
    include/multi/details/mpmcqueue.h
    include/multi/details/task.h
    include/multi/details/workstealdeque.h
    include/multi/details/workerpool.h
    include/multi/details/workerpool.inl
    include/multi/details/handle.inl
    include/multi/context.h
    include/multi/handle.h
    include/multi/multi.h

    # Source
    src/context.cpp
    src/multi.cpp
    src/workstealdeque.cpp
    src/workerpool.cpp
)
# Generate <multi/version.h> from the project version declared above.
# Lives in the build tree so it's regenerated when the version changes
# without dirtying the source tree.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/include/multi/version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/include/multi/version.h
    @ONLY
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(${PROJECT_NAME} PUBLIC Threads::Threads)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

# Namespaced alias so consumers can always write multi::multi regardless of
# whether they got the target via add_subdirectory or find_package.
add_library(multi::multi ALIAS ${PROJECT_NAME})
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
        test/handle.cpp
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

# Examples — one executable per file in examples/, named example-<stem>.
# Each example is self-validating: prints what it does and exits non-zero
# if a result check fails. Useful as documentation and as a smoke check
# (`for f in build/example-*; do $f || break; done`).
if(MULTI_BUILD_EXAMPLES)
    set(MULTI_EXAMPLES hello parallel_for reduce fanout nested)
    foreach(example IN LISTS MULTI_EXAMPLES)
        add_executable(example-${example} examples/${example}.cpp)
        target_link_libraries(example-${example} PRIVATE ${PROJECT_NAME})
        target_compile_features(example-${example} PRIVATE cxx_std_17)
    endforeach()
endif()

# Install / Export
if(MULTI_INSTALL)
    include(CMakePackageConfigHelpers)

    install(TARGETS ${PROJECT_NAME}
        EXPORT multiTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    )

    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "*.in" EXCLUDE
    )

    # Generated version header lives in the build tree — install it
    # explicitly so downstream `find_package(multi)` consumers can include it.
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/include/multi/version.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/multi
    )

    install(EXPORT multiTargets
        FILE multiTargets.cmake
        NAMESPACE multi::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/multi
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/multi-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/multi-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/multi
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/multi-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/multi-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/multi-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/multi
    )
endif()
