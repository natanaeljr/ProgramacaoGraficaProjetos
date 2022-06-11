cmake_minimum_required(VERSION 3.5)
project(ProgGraf LANGUAGES C CXX)

# This project can be built as a standalone project on a system with the required
# libraries already installed, or it can be part of the Super-Project which builds all
# dependencies with the correct versions.
# For building it separately with the dependency builds from the Super-Project,
# the variable `CMAKE_PREFIX_PATH` must be set to the Super-Project's build/external dir.
# In Linux you can do it as:
#  cmake .. -DCMAKE_PREFIX_PATH=$(realpath ../../build/external)

#########################################################################################
# Configuration
#########################################################################################
# Default build type to Debug, for release use `cmake -DCMAKE_BUILD_TYPE=Release ..`
if(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type default to Debug" FORCE)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")

# Turn on when having trouble building
set(CMAKE_VERBOSE_MAKEFILE OFF)
# Compilation Database for C++ language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# For OpenGL prefer new GL Vendor Neutral Dispatch (GLVND) rather than legacy
set(OpenGL_GL_PREFERENCE "GLVND")

# Default compilation settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS_DEBUG "-g -ggdb -pg -fsanitize=undefined -Wall")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

#########################################################################################
# Dependencies
#########################################################################################
# OpenGL for graphics library
find_package(OpenGL REQUIRED)
# GLFW for desktop windowing and input
find_package(glfw3 REQUIRED)
# GLEW for C++ binding of the OpenGL API
find_package(GLEW REQUIRED)

find_package(Threads REQUIRED)

#########################################################################################
# Project
#########################################################################################
#add_subdirectory(game)

# Main target binary
add_executable(game)
target_sources(game PRIVATE
  supermario.cpp
  stb_image.cpp
)
target_link_libraries(game PRIVATE
  GLEW::glew
  glfw
  OpenGL::GL
  Threads::Threads
  ${CMAKE_DL_LIBS}
)
target_compile_definitions(game PRIVATE
  GLFW_INCLUDE_NONE
  ASSETS_PATH="${CMAKE_CURRENT_SOURCE_DIR}"
)
