################################################################################
#
# @file Makefile
#
# @brief Set of commands to create and remove object files
#
# @author Tarun Malviya <malviya.t@northeastern.edu>
#
################################################################################

CXX ?= g++

# path #
SRC_PATH = src
INCLUDE_PATH = include
BUILD_PATH = build

# extensions #
SRC_EXT = cpp

# code lists #
# Find all source files in the source directory, sorted by
# most recently modified
SOURCES = $(shell find $(SRC_PATH) -name '*.$(SRC_EXT)' | sort -k 1nr | cut -f2-)
# Set the object file names, with the source directory stripped
# from the path, and the build path prepended in its place
OBJECTS = $(SOURCES:$(SRC_PATH)/%.$(SRC_EXT)=$(BUILD_PATH)/%.o)
# Set the dependency files that will be used to add header dependencies
DEPS = $(OBJECTS:.o=.d)

# flags #
COMPILE_FLAGS = -std=c++17 -Wall -Wextra -g -w -fPIC
INCLUDES = -I include/ -I /usr/local/include
# Space-separated pkg-config libraries used by this project
LIBS =

.PHONY: default_target
default_target: release

.PHONY: release
release: export CXXFLAGS := $(CXXFLAGS) $(COMPILE_FLAGS)
release: dirs
	@$(MAKE) all

.PHONY: dirs
dirs:
	@echo "Creating directories"
	@mkdir -p $(BUILD_PATH)

.PHONY: clean
clean:
	@echo "Deleting directories"
	@$(RM) -r $(BUILD_PATH)

.PHONY: all
all: $(BUILD_PATH)/libvcdwriter.so

# Creation of the shared library
$(BUILD_PATH)/libvcdwriter.so: $(OBJECTS)
	@echo "Building shared library: $@"
	${CXX} $(CXXFLAGS) $(INCLUDES) -shared -o $@ $^

# Add dependency files, if they exist
-include $(DEPS)

# Source file rules
# After the first compilation they will be joined with the rules from the
# dependency files to provide header dependencies
$(BUILD_PATH)/%.o: $(SRC_PATH)/%.$(SRC_EXT)
	@echo "Compiling: $< -> $@"
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LIBS) -MP -MMD -c $< -o $@
