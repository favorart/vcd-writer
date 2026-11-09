# vcd-writer

Port PyVCD package from python to C++

Visit [PyVCD on GitHub](https://github.com/SanDisk-Open-Source/pyvcd/).

It writes Value Change Dump (VCD) files as specified in IEEE 1364-2005.

## Requirements

A C++20 compiler with `std::format` support (GCC 13+, Clang 17+ with libc++, MSVC 19.29+ / Visual Studio 2019 16.10+).
The library has no third-party dependencies; GoogleTest is fetched only to build the unit tests.

## Versions

Every commit on `master` that passes CI is tagged automatically as `vMAJOR.MINOR.PATCH`
by [github-tag-action](https://github.com/mathieudutour/github-tag-action): the patch number is bumped
by default, `feat:` commits bump the minor number and `BREAKING CHANGE` bumps the major one.
Use a tag instead of `master` in `GIT_TAG` to pin a version.
Each tag gets a [GitHub Release](https://github.com/favorart/vcd-writer/releases) with prebuilt Release binaries
(header, static and shared libraries) for Linux, Windows and macOS.

## Install with CMake
Create a `CMakeLists.txt` file on the root directory of your project and use the `CPMAddPackage` command to include this library in your project.
Here's an example of a CMakeLists.txt file:
```
cmake_minimum_required(VERSION 3.14)
project(
    projectName
    VERSION 2025.0.1 
    LANGUAGES CXX
)

include(cmake/CPM.cmake)

CPMAddPackage(
    NAME libvcdwriter
    GITHUB_REPOSITORY favorart/vcd-writer 
    GIT_TAG master
    DOWNLOAD_ONLY TRUE
    options
        "VCDWRITER_BUILD_MAIN OFF"
        "VCDWRITER_BUILD_TESTS OFF"
)

add_subdirectory(${libvcdwriter_SOURCE_DIR})

add_executable(mainApp ../main.cpp)
target_link_libraries(mainApp PRIVATE vcdwriter::vcdwriter_shared)
 
```

## Install with Make

```
# install gtests (ubuntu)
sudo apt-get install libgtest-dev

# compile library and tests (requires g++ >= 13 for <format>)
make

# run simple test
./build/main

# run unit tests
./build/test
```

Default result:

```
build/libvcdwriter.a    # static library
build/libvcdwriter.so   # shared library
build/main              # simple test execution file
build/test              # unit tests  execution file
```


## Run tests with sanitizers

```
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DVCDWRITER_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Run the benchmark

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DVCDWRITER_BUILD_BENCH=ON
cmake --build build --config Release
./build/vcd_bench
```

## Python

The `vcdwriter` module is a thin [nanobind](https://github.com/wjakob/nanobind) wrapper of the C++ API: the same classes, methods, enumerations and values. Python 3.8+.

```
pip install git+https://github.com/favorart/vcd-writer
```

or build it with CMake (`pip install nanobind`, then `-DVCDWRITER_BUILD_PYTHON=ON`; ctest runs its tests).

```python
from vcdwriter import Writer, VariableType

with Writer("dump.vcd") as writer:
    counter = writer.register_var("a.b.c", "counter", VariableType.integer, 8)
    clk = writer.register_var("a.b", "clk", VariableType.integer, 1)
    for timestamp in range(5):
        writer.change(counter, timestamp, format(10 + timestamp * 2, "b"))  # VCD strings, as in C++
        writer.change(clk, timestamp, "1" if timestamp % 2 == 0 else "0")
```

Errors are `vcdwriter.VCDPhaseError` (call order) and `vcdwriter.VCDTypeError` (invalid value), both derived from `vcdwriter.VCDError`.

Speed against pyvcd (`python bench/vcd_bench.py`: 1000 vectors of 32 bits and 1000 scalars change on each of 200 timestamps, time per change including the Python loop):

| Scenario | pyvcd 0.5.1 | vcdwriter | Speedup |
|---|---:|---:|---:|
| str values, all changed | 2130 ns | 360 ns | 5.9× |
| str values, unchanged (deduplicated) | 340 ns | 300 ns | 1.1× |

Linux, Python 3.12, GCC 13.

## Quick Start

```C++
#include "vcd_writer.h"

using namespace vcd;

HeadPtr head = make_header(TimeScale::one, TimeScaleUnit::ns, utils::now());
std::string filename = "dump.vcd";
Writer writer(filename, head);

VarPtr counter_var = writer.register_var("a.b.c", "counter", VariableType::integer, 8);
VarPtr var_var = writer.register_var("a.b", "var", VariableType::integer, 8);

for (int timestamp = 0; timestamp < 5; ++timestamp)
{
	const int c_val = 10 + timestamp * 2;
	const int v_val = 11 + timestamp * 2;
	writer.change(counter_var, timestamp, std::bitset<8>(c_val).to_string());
	writer.change(var_var, timestamp, std::bitset<8>(v_val).to_string());
}
```

**Output:**

	$timescale 1 ns $end
	$date 2022-04-18 11:12:38 $end
	$scope module a $end
	$scope module b $end
	$var integer 8 1 var $end
	$scope module c $end
	$var integer 8 0 counter $end
	$upscope $end
	$upscope $end
	$upscope $end
	$enddefinitions $end
	#0
	$dumpvars
	b00001010 0
	b00001011 1
	$end
	#1
	b00001100 0
	b00001101 1
	#2
	b00001110 0
	b00001111 1
	#3
	b00010000 0
	b00010001 1
	#4
	b00010010 0
	b00010011 1

