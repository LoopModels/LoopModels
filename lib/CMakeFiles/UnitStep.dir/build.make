# CMAKE generated file: DO NOT EDIT!
# Generated by "Unix Makefiles" Generator, CMake Version 3.16

# Delete rule output on recipe failure.
.DELETE_ON_ERROR:


#=============================================================================
# Special targets provided by cmake.

# Disable implicit rules so canonical targets will work.
.SUFFIXES:


# Remove some rules from gmake that .SUFFIXES does not remove.
SUFFIXES =

.SUFFIXES: .hpux_make_needs_suffix_list


# Suppress display of executed commands.
$(VERBOSE).SILENT:


# A target that is always out of date.
cmake_force:

.PHONY : cmake_force

#=============================================================================
# Set environment variables for the build.

# The shell in which to execute make rules.
SHELL = /bin/sh

# The CMake executable.
CMAKE_COMMAND = /usr/bin/cmake

# The command to remove a file.
RM = /usr/bin/cmake -E remove -f

# Escaping for special characters.
EQUALS = =

# The top-level source directory on which CMake was run.
CMAKE_SOURCE_DIR = /home/sumiya11/loops/try2/LoopModels/lib

# The top-level build directory on which CMake was run.
CMAKE_BINARY_DIR = /home/sumiya11/loops/try2/LoopModels/lib

# Include any dependencies generated for this target.
include CMakeFiles/UnitStep.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/UnitStep.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/UnitStep.dir/flags.make

CMakeFiles/UnitStep.dir/UnitStep.cpp.o: CMakeFiles/UnitStep.dir/flags.make
CMakeFiles/UnitStep.dir/UnitStep.cpp.o: UnitStep.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/sumiya11/loops/try2/LoopModels/lib/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/UnitStep.dir/UnitStep.cpp.o"
	/usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/UnitStep.dir/UnitStep.cpp.o -c /home/sumiya11/loops/try2/LoopModels/lib/UnitStep.cpp

CMakeFiles/UnitStep.dir/UnitStep.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/UnitStep.dir/UnitStep.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/sumiya11/loops/try2/LoopModels/lib/UnitStep.cpp > CMakeFiles/UnitStep.dir/UnitStep.cpp.i

CMakeFiles/UnitStep.dir/UnitStep.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/UnitStep.dir/UnitStep.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/sumiya11/loops/try2/LoopModels/lib/UnitStep.cpp -o CMakeFiles/UnitStep.dir/UnitStep.cpp.s

# Object files for target UnitStep
UnitStep_OBJECTS = \
"CMakeFiles/UnitStep.dir/UnitStep.cpp.o"

# External object files for target UnitStep
UnitStep_EXTERNAL_OBJECTS =

libUnitStep.so: CMakeFiles/UnitStep.dir/UnitStep.cpp.o
libUnitStep.so: CMakeFiles/UnitStep.dir/build.make
libUnitStep.so: CMakeFiles/UnitStep.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/sumiya11/loops/try2/LoopModels/lib/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX shared module libUnitStep.so"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/UnitStep.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/UnitStep.dir/build: libUnitStep.so

.PHONY : CMakeFiles/UnitStep.dir/build

CMakeFiles/UnitStep.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/UnitStep.dir/cmake_clean.cmake
.PHONY : CMakeFiles/UnitStep.dir/clean

CMakeFiles/UnitStep.dir/depend:
	cd /home/sumiya11/loops/try2/LoopModels/lib && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib/CMakeFiles/UnitStep.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/UnitStep.dir/depend

