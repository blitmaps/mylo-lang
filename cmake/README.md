# Mylo CMake Integration Guide
This guide explains how to integrate the Mylo programming language 
with CMake using the MyloUtils.cmake module. This allows you to easily
build native bindings (C/C++ extensions) and standalone Mylo applications.

## 1. Prerequisites

Before starting, ensure you have the following installed and configured:

- CMake (3.14+)

- C Compiler (GCC, Clang, or MSVC)

- Mylo Language Source: You need the path to the Mylo repository (for runtime headers/sources).

- Mylo Compiler Binary: A built mylo executable. (this can be built using the CMake file in the root of Mylo)

## 2. Setup

Copy cmake/MyloUtils.cmake into your project's cmake/ folder.

In your root CMakeLists.txt, you must define two variables 
before including the module:

```cmake
# Path to the root of the Mylo Language repository
set(MYLO_HOME "/path/to/mylo-lang")

# Path to the built mylo executable
set(MYLO_EXECUTABLE "${MYLO_HOME}/build/mylo")

# Include the module
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MyloUtils)
```

## 3. Creating a Native Binding
Use mylo_add_binding to compile a .mylo file 
containing import C blocks into a shared library (.so or .dll) that 
can be loaded by the Mylo interpreter.

```cmake
mylo_add_binding(
    TARGET <NameOfLibrary>
    SOURCE <PathToBindingFile.mylo>
    LINKS  <List of C Libraries to Link>
)
```
#### Example: If you are wrapping the raylib library with a binding file named raylib_binding.mylo

```cmake
# 1. Find the external C library
find_package(raylib REQUIRED)

# 2. Define the binding
mylo_add_binding(
    TARGET raylib_binding           # Creates raylib_binding.so / .dll
    SOURCE raylib_binding.mylo      # The source file with 'import C'
    LINKS  raylib                   # Link against the actual C library
)
```
#### What this does:

- Runs `mylo --bind raylib_binding.mylo` to generate raylib_binding.mylo_bind.c.

- Compiles that C file into a shared library.

- Links it against raylib.

- Removes the lib prefix (on Unix) so import native "raylib_binding.mylo" can load it.

## 4. Creating a Standalone Executable

Use mylo_add_executable to transpile a .mylo script into C and compile it into a standalone binary. 
This binary includes the full Mylo VM runtime.

```cmake
mylo_add_executable(
    TARGET <NameOfApp>
    SOURCE <PathToMain.mylo>
)
```

#### Example:

```cmake
mylo_add_executable(
    TARGET my_game
    SOURCE src/main.mylo
)

# If your app relies on the binding created above, make sure it builds first:
add_dependencies(my_game raylib_binding)
```

#### What this does:

- Runs `mylo --build src/main.mylo` to generate `main.c`.

- Compiles main.c along with vm.c, utils.c, and mylolib.c.

- Links the standard math library (-lm).

## 5. Full Project Example
Project Structure:

````text
my_project/
├── CMakeLists.txt
├── cmake/
│   └── MyloUtils.cmake
├── lib/
│   └── raylib_binding.mylo  <-- Library wrapper
└── src/
    └── main.mylo            <-- Main application
````

#### CMakeLists.txt (Example)

```cmake
cmake_minimum_required(VERSION 3.14)
project(MyGame)

# --- Config ---
set(MYLO_HOME "/home/user/repos/mylo-lang")
set(MYLO_EXECUTABLE "${MYLO_HOME}/build/mylo")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MyloUtils)

# --- Dependencies ---
find_package(raylib REQUIRED)

# --- 1. Build the Binding ---
mylo_add_binding(
    TARGET raylib_native
    SOURCE lib/raylib_binding.mylo
    LINKS  raylib
)

# --- 2. Build the Game ---
mylo_add_executable(
    TARGET game_app
    SOURCE src/main.mylo
)

# Ensure binding is built before the app runs (optional but recommended)
add_dependencies(game_app raylib_native)
```
