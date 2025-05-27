# vcd-writer

Port PyVCD package from python to C++

Visit [PyVCD on GitHub](https://github.com/SanDisk-Open-Source/pyvcd/).

It writes Value Change Dump (VCD) files as specified in IEEE 1364-2005.

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
    GITHUB_REPOSITORY harris2001/vcd-converter
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

# compile library and tests
make

# run simple test
./test/main

# run unit tests
./test/test
```

Default result:

```
build/libvcdwriter.a    # static library
build/libvcdwriter.so   # shared library
build/main              # simple test execution file
build/test              # unit tests  execution file
```


## Quick Start

```C++
	#include "vcd_writer.h"
	using namespace vcd;

	HeadPtr head = makeVCDHeader(TimeScale::ONE, TimeScaleUnit::ns, utils::now());
	std::string filename = "dump.vcd";
	VCDWriter writer(filename, head);
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
	$upscope $end
	$scope module b $end
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

