# cmake/MyloUtils.cmake

# Ensure we have the Mylo compiler executable
if(NOT DEFINED MYLO_EXECUTABLE)
    message(FATAL_ERROR "MYLO_EXECUTABLE must be set to the path of the 'mylo' compiler binary.")
endif()

# Ensure we know where the VM sources are (required for linking the runtime)
if(NOT DEFINED MYLO_HOME)
    message(FATAL_ERROR "MYLO_HOME must be set to the root directory of the Mylo language source.")
endif()

set(MYLO_VM_SOURCES
        "${MYLO_HOME}/src/vm.c"
        "${MYLO_HOME}/src/utils.c"
        "${MYLO_HOME}/src/mylolib.c"
)

# --- FUNCTION: Create a Native Binding (.so/.dll) ---
# Usage: mylo_add_binding(TARGET raylib_native SOURCE raylib_binding.mylo LINKS raylib)
function(mylo_add_binding)
    set(options "")
    set(oneValueArgs TARGET SOURCE)
    set(multiValueArgs LINKS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    # 1. Define Output Filename (raylib_binding.mylo -> raylib_binding.mylo_bind.c)
    get_filename_component(BASENAME ${ARG_SOURCE} NAME)
    set(GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}_bind.c")

    # 2. Command: Run 'mylo --bind'
    add_custom_command(
            OUTPUT ${GENERATED_C}
            COMMAND ${MYLO_EXECUTABLE} --bind ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SOURCE}
            ARGS ${CMAKE_CURRENT_BINARY_DIR}/${GENERATED_C} # Depending on how your CLI handles output paths
            DEPENDS ${ARG_SOURCE}
            COMMENT "Generating Mylo Binding: ${ARG_SOURCE}"
    )

    # 3. Create Shared Library
    add_library(${ARG_TARGET} SHARED ${GENERATED_C})

    # 4. Remove 'lib' prefix so 'import native' finds it easily (libfoo.so -> foo.so)
    set_target_properties(${ARG_TARGET} PROPERTIES PREFIX "")

    # 5. Link Dependencies (e.g. Raylib) and Include Mylo Headers
    target_link_libraries(${ARG_TARGET} PRIVATE ${ARG_LINKS})
    target_include_directories(${ARG_TARGET} PRIVATE "${MYLO_HOME}/src")

    # 6. Copy to build root so the app can find it at runtime
    add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${ARG_TARGET}>
            ${CMAKE_BINARY_DIR}/$<TARGET_FILE_NAME:${ARG_TARGET}>
    )
endfunction()


# --- FUNCTION: Create a Standalone Executable ---
# Usage: mylo_add_executable(TARGET my_app SOURCE main.mylo)
function(mylo_add_executable)
    set(options "")
    set(oneValueArgs TARGET SOURCE)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(BASENAME ${ARG_SOURCE} NAME)
    set(GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}.c")

    # 1. Command: Run 'mylo --build'
    add_custom_command(
            OUTPUT ${GENERATED_C}
            COMMAND ${MYLO_EXECUTABLE} --build ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SOURCE}
            DEPENDS ${ARG_SOURCE}
            COMMENT "Transpiling Mylo Source: ${ARG_SOURCE}"
    )

    # 2. Create Executable
    # We MUST include the VM sources (vm.c, mylolib.c) so the app has a runtime.
    add_executable(${ARG_TARGET} ${GENERATED_C} ${MYLO_VM_SOURCES})

    # 3. Setup Includes and Standard Math Lib
    target_include_directories(${ARG_TARGET} PRIVATE "${MYLO_HOME}/src")

    if(UNIX)
        target_link_libraries(${ARG_TARGET} PRIVATE m)
    endif()
endfunction()