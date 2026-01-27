# cmake/MyloUtils.cmake

if(NOT DEFINED MYLO_EXECUTABLE)
    message(FATAL_ERROR "MYLO_EXECUTABLE must be set.")
endif()

if(NOT DEFINED MYLO_HOME)
    message(FATAL_ERROR "MYLO_HOME must be set.")
endif()

set(MYLO_VM_SOURCES
        "${MYLO_HOME}/src/vm.c"
        "${MYLO_HOME}/src/utils.c"
        "${MYLO_HOME}/src/mylolib.c"
)

# --- FUNCTION: Create a Native Binding (.so/.dll) ---
function(mylo_add_binding)
    set(options "")
    set(oneValueArgs TARGET SOURCE)
    set(multiValueArgs LINKS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(BASENAME ${ARG_SOURCE} NAME)

    # Paths in the Build Directory
    set(LOCAL_MYLO "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}")
    set(GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}_bind.c")

    # 1. Copy .mylo file to Build Dir (so generated .c file ends up here too)
    add_custom_command(
            OUTPUT ${GENERATED_C}

            # Step A: Copy source to build folder
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SOURCE}
            ${LOCAL_MYLO}

            # Step B: Run mylo --bind on the LOCAL copy
            COMMAND ${MYLO_EXECUTABLE} --bind ${BASENAME}

            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            DEPENDS ${ARG_SOURCE}
            COMMENT "Generating Binding Wrapper: ${ARG_SOURCE}"
    )

    # 2. Compile the generated C file into a Shared Library
    add_library(${ARG_TARGET} SHARED ${GENERATED_C})

    set_target_properties(${ARG_TARGET} PROPERTIES LINKER_LANGUAGE C)

    # 3. Configure Output (No 'lib' prefix, standard extension)
    set_target_properties(${ARG_TARGET} PROPERTIES PREFIX "")

    target_link_libraries(${ARG_TARGET} PRIVATE ${ARG_LINKS})
    target_include_directories(${ARG_TARGET} PRIVATE "${MYLO_HOME}/src")

    # 4. Auto-copy the .so/.dll next to the executable if needed
    add_custom_command(TARGET ${ARG_TARGET} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${ARG_TARGET}>
            ${CMAKE_BINARY_DIR}/$<TARGET_FILE_NAME:${ARG_TARGET}>
    )
endfunction()

# --- FUNCTION: Create a Standalone Executable ---
# --- FUNCTION: Create a Standalone Executable ---
function(mylo_add_executable)
    set(options "")
    set(oneValueArgs TARGET SOURCE)
    set(multiValueArgs "")
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    get_filename_component(BASENAME ${ARG_SOURCE} NAME)
    set(LOCAL_MYLO "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}")

    # CMake expects this filename:
    set(GENERATED_C "${CMAKE_CURRENT_BINARY_DIR}/${BASENAME}.c")

    add_custom_command(
            OUTPUT ${GENERATED_C}

            # 1. Copy source to build folder
            COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_CURRENT_SOURCE_DIR}/${ARG_SOURCE}
            ${LOCAL_MYLO}

            # 2. Run mylo --build (Generates 'out.c')
            COMMAND ${MYLO_EXECUTABLE} --build ${BASENAME}

            # 3. FIX: Rename 'out.c' to 'raylib.mylo.c' so CMake finds it
            COMMAND ${CMAKE_COMMAND} -E rename out.c ${BASENAME}.c

            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            DEPENDS ${ARG_SOURCE}
            COMMENT "Transpiling Mylo Source: ${ARG_SOURCE}"
    )

    add_executable(${ARG_TARGET} ${GENERATED_C} ${MYLO_VM_SOURCES})

    set_target_properties(${ARG_TARGET} PROPERTIES LINKER_LANGUAGE C)

    target_include_directories(${ARG_TARGET} PRIVATE "${MYLO_HOME}/src")

    if(UNIX)
        target_link_libraries(${ARG_TARGET} PRIVATE m)
    endif()
endfunction()
