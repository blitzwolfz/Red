cmake_minimum_required(VERSION 3.16)
project(red LANGUAGES C CXX VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RED_SANITIZE "Build with the address and undefined behaviour sanitizers" OFF)

add_executable(red
  src/main.cpp
  src/chunk.cpp
  src/compiler.cpp
  src/debug.cpp
  src/runtime.cpp
  src/scanner.cpp
  src/serialize.cpp
  src/table.cpp
  src/util.cpp
  src/value.cpp
  src/vm.cpp
  src/stdlib/registry.cpp
  src/stdlib/core.cpp
  src/stdlib/io.cpp
  src/stdlib/os.cpp
  src/stdlib/net.cpp
  src/stdlib/conc.cpp
  src/stdlib/ffi.cpp
  src/stdlib/legacy.cpp
)

target_compile_options(red PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Extensions loaded through the FFI resolve red_* helpers in this binary at
# load time, so its symbols have to stay in the dynamic symbol table.
set_target_properties(red PROPERTIES ENABLE_EXPORTS ON)

find_package(Threads REQUIRED)
target_link_libraries(red PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(RED_SANITIZE)
  target_compile_options(red PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer -g)
  target_link_options(red PRIVATE -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------
# The v1 Java interpreter, kept so old scripts still run. Built only when
# a JDK is present; everything else works without one.
find_program(JAVAC_EXECUTABLE javac)
find_program(JAR_EXECUTABLE jar)

if(JAVAC_EXECUTABLE AND JAR_EXECUTABLE)
  file(GLOB LEGACY_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/legacy/redlang/*.java)
  set(LEGACY_JAR ${CMAKE_CURRENT_BINARY_DIR}/red-legacy.jar)
  add_custom_command(
    OUTPUT ${LEGACY_JAR}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/legacy-classes
    COMMAND ${JAVAC_EXECUTABLE} -d ${CMAKE_CURRENT_BINARY_DIR}/legacy-classes ${LEGACY_SOURCES}
    COMMAND ${JAR_EXECUTABLE} cf ${LEGACY_JAR} -C ${CMAKE_CURRENT_BINARY_DIR}/legacy-classes .
    DEPENDS ${LEGACY_SOURCES}
    COMMENT "Building the v1 interpreter into red-legacy.jar"
    VERBATIM
  )
  add_custom_target(red-legacy ALL DEPENDS ${LEGACY_JAR})
  add_dependencies(red red-legacy)
else()
  message(STATUS "No JDK found: skipping red-legacy.jar. `red legacy` will report that it is missing.")
endif()

# ---------------------------------------------------------------------
# Example FFI extension, built so that the test suite can load it.
add_library(red_example_ext MODULE ffi/example_ext.c)
set_target_properties(red_example_ext PROPERTIES
  PREFIX ""
  OUTPUT_NAME "example_ext"
  C_STANDARD 11
)
target_include_directories(red_example_ext PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ffi)
if(APPLE)
  # The helper functions live in the red binary, not in this module.
  target_link_options(red_example_ext PRIVATE -undefined dynamic_lookup)
endif()

enable_testing()
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME conformance
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run.py
            --red $<TARGET_FILE:red>
            --tests ${CMAKE_CURRENT_SOURCE_DIR}/tests
  )
endif()
