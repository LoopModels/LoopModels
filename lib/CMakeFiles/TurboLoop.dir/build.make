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
include CMakeFiles/TurboLoop.dir/depend.make

# Include the progress variables for this target.
include CMakeFiles/TurboLoop.dir/progress.make

# Include the compile flags for this target's objects.
include CMakeFiles/TurboLoop.dir/flags.make

CMakeFiles/TurboLoop.dir/TurboLoop.cpp.o: CMakeFiles/TurboLoop.dir/flags.make
CMakeFiles/TurboLoop.dir/TurboLoop.cpp.o: TurboLoop.cpp
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --progress-dir=/home/sumiya11/loops/try2/LoopModels/lib/CMakeFiles --progress-num=$(CMAKE_PROGRESS_1) "Building CXX object CMakeFiles/TurboLoop.dir/TurboLoop.cpp.o"
	/usr/bin/c++  $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -o CMakeFiles/TurboLoop.dir/TurboLoop.cpp.o -c /home/sumiya11/loops/try2/LoopModels/lib/TurboLoop.cpp

CMakeFiles/TurboLoop.dir/TurboLoop.cpp.i: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Preprocessing CXX source to CMakeFiles/TurboLoop.dir/TurboLoop.cpp.i"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -E /home/sumiya11/loops/try2/LoopModels/lib/TurboLoop.cpp > CMakeFiles/TurboLoop.dir/TurboLoop.cpp.i

CMakeFiles/TurboLoop.dir/TurboLoop.cpp.s: cmake_force
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green "Compiling CXX source to assembly CMakeFiles/TurboLoop.dir/TurboLoop.cpp.s"
	/usr/bin/c++ $(CXX_DEFINES) $(CXX_INCLUDES) $(CXX_FLAGS) -S /home/sumiya11/loops/try2/LoopModels/lib/TurboLoop.cpp -o CMakeFiles/TurboLoop.dir/TurboLoop.cpp.s

# Object files for target TurboLoop
TurboLoop_OBJECTS = \
"CMakeFiles/TurboLoop.dir/TurboLoop.cpp.o"

# External object files for target TurboLoop
TurboLoop_EXTERNAL_OBJECTS =

libTurboLoop.so: CMakeFiles/TurboLoop.dir/TurboLoop.cpp.o
libTurboLoop.so: CMakeFiles/TurboLoop.dir/build.make
libTurboLoop.so: CMakeFiles/TurboLoop.dir/link.txt
	@$(CMAKE_COMMAND) -E cmake_echo_color --switch=$(COLOR) --green --bold --progress-dir=/home/sumiya11/loops/try2/LoopModels/lib/CMakeFiles --progress-num=$(CMAKE_PROGRESS_2) "Linking CXX shared module libTurboLoop.so"
	$(CMAKE_COMMAND) -E cmake_link_script CMakeFiles/TurboLoop.dir/link.txt --verbose=$(VERBOSE)

# Rule to build all files generated by this target.
CMakeFiles/TurboLoop.dir/build: libTurboLoop.so

.PHONY : CMakeFiles/TurboLoop.dir/build

CMakeFiles/TurboLoop.dir/clean:
	$(CMAKE_COMMAND) -P CMakeFiles/TurboLoop.dir/cmake_clean.cmake
.PHONY : CMakeFiles/TurboLoop.dir/clean

CMakeFiles/TurboLoop.dir/depend:
	cd /home/sumiya11/loops/try2/LoopModels/lib && $(CMAKE_COMMAND) -E cmake_depends "Unix Makefiles" /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib /home/sumiya11/loops/try2/LoopModels/lib/CMakeFiles/TurboLoop.dir/DependInfo.cmake --color=$(COLOR)
.PHONY : CMakeFiles/TurboLoop.dir/depend

