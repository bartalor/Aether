cmake_minimum_required(VERSION 3.20)
project(aether VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Require C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Use -std=c++20, not -std=gnu++20

# Compiler warnings — catch problems early
add_compile_options(-Wall -Wextra -Wpedantic)

# Shared include directory (public headers used across all components)
include_directories(${PROJECT_SOURCE_DIR}/include)

# Components
add_subdirectory(lib)
add_subdirectory(daemon)
add_subdirectory(cli)
add_subdirectory(tests)
