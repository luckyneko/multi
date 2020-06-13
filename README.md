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
- Job completion tracking
- Parallel & Sequential tasks
- Single threaded mode
- Only write functions once for single & multi threaded

## Examples
For extra examples please see `tests/jobcontext.cpp`

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
    multi::Handle jobHdl = multi::async([&](JobContext&)
    {
        ++i;
    });

    // Wait for job to complete
    jobHdl.wait();

    // async will automatically wait if handle not captured
    multi::async([&](JobContext&)
    {
        ++i;
    });

    // Wait for all jobs, and close threads.
    multi::stop();
}
```

### Parallel & Sequential child tasks
``` C++
void function()
{
    // Automatically wait for async
    multi::async([](JobContext& jc)
    {
        // Launch all as child tasks in parallel
        jc.add(multi::Order::par, 
            [](JobContext&)
            {

            },
            [](JobContext&)
            {

            }
        );

        // Launch as a sequence of child tasks
        jc.add(multi::Order::seq, 
            [](JobContext&)
            {

            },
            [](JobContext&)
            {

            }
        );
    });
}
```

### Job object / Task Wrapper
``` C++
multi::Job function(const std::vector<int>& indices)
{
    return Job([indices](JobContext& jc)
    {
        // For each index do something
        jc.each(multi::Order::par, indices.begin(), indices.end(),
            [](JobContext&, int value)
            {
                // Do work per index
            }
        );
    });
}

void run(const std::vector<int>& indices)
{
    // Run function single-threaded
    function(indices);

    // Run function multi-threaded
    multi::async( function(indices) );

    // Run function as child task
    multi::async([&](JobContext& jc)
    {
        jc.add( function(jc) );
    });
}
```