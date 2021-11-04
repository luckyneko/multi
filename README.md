# multi [![Build Status](https://travis-ci.org/luckyneko/multi.svg?branch=master)](https://travis-ci.org/luckyneko/multi)&nbsp; ![Release](https://img.shields.io/github/v/release/luckyneko/multi?include_prereleases)
C++11 real-time threading framework

## Build / Integrate

multi is a static library only. It is **recommended** to submodule or copy into your repo.

### To build stand-alone:
``` sh
git clone https://github.com/luckyneko/multi.git
cd multi
cmake -B build
cmake --build build
```

### To add to your CMakeLists.txt
``` cmake
add_subdirectory("path/to/multi")
target_link_libraries(${PROJECT_NAME} multi)
```

## Tested Platforms
- Linux (GCC 4.8 & 7, Clang 3.5 & 10)
- Windows (MSVC 2017)
- MacOS (clang 11)
- NB: In theory works on iOS & Android

## Features
- Simple in-line API
- Will run on local-thread as well as workers
- Single threaded mode
- Only write functions once for single & multi threaded

## Examples
For extra examples please see `tests/context.cpp`

### Basic Async
``` C++
#include <multi/multi.h>
#include <atomic>

int main()
{
    // Start with hardware thread count
    multi::start();

    // Run job
    std::atomic<int> i(0);
    multi::Handle jobHdl = multi::async([&]()
    {
        ++i;
    });

    // Wait for job to complete
    jobHdl.wait();

    // async will automatically wait if handle not captured
    multi::async([&]()
    {
        ++i;
    });

    // Wait for all jobs, and close threads.
    multi::stop();
}
```

### Parallel tasks
``` C++
void function()
{
    // Run 2 tasks side by side
    multi::parallel(
        // Task 1
        []()
        {
        },
        // Task 2
        []()
        {
        }
    );
}
```

### Run function on Each item
``` C++
void function()
{
    std::vector<Item> items;

    // Run task per item
    multi::each(items.begin(), items.end(), [](Item& item)
    {
        item.setValue();
    };

    // Run over all items, using 32 tasks
    multi::each(32, items.end(), [](Item& item)
    {
        item.setValue();
    };
}
```

### Run function on range of numbers with step
``` C++
void function()
{

    // Run task per step
    multi::range(0, 100, 2, [](int idx)
    {
        out += idx;
    };

    // Run task per step, using 32 tasks
    multi::each(32, 0, 100, 2, [](int idx)
    {
        out += idx;
    };
}
```
