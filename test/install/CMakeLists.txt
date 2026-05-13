cmake_minimum_required(VERSION 3.15)
project(multi-install-test LANGUAGES CXX)

# CI smoke-test: this fixture is built standalone against a previously
# installed `multi` package. It verifies that `cmake --install` produced
# a usable layout — config file present, exported alias works, headers
# include cleanly, version macros land.
find_package(multi REQUIRED CONFIG)

add_executable(consumer consumer.cpp)
target_link_libraries(consumer PRIVATE multi::multi)
target_compile_features(consumer PRIVATE cxx_std_17)
