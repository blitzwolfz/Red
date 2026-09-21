cmake_minimum_required(VERSION 3.16)
project(red LANGUAGES C CXX VERSION 0.4.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RED_SANITIZE "Build with the address and undefined behaviour sanitizers" OFF)
option(RED_SANITIZE_THREAD "Build with the thread sanitizer" OFF)

add_executable(red
  src/main.cpp
  src/bundle.cpp
  src/chunk.cpp
  src/compiler.cpp
  src/debug.cpp
  src/debugger.cpp
  src/format.cpp
  src/runtime.cpp
  src/objlock.cpp
  src/regex.cpp
  src/scanner.cpp
  src/serialize.cpp
  src/table.cpp
  src/types.cpp
  src/unicode.cpp
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
  src/stdlib/process.cpp
  src/stdlib/re.cpp
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

# Separate from the one above, because the two cannot be combined. This
# is what the parallel tests are run under.
if(RED_SANITIZE_THREAD)
  target_compile_options(red PRIVATE -fsanitize=thread -fno-omit-frame-pointer -g)
  target_link_options(red PRIVATE -fsanitize=thread)
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
# Extensions. red_add_extension() is the same helper another project
# would include to build one of its own; see docs/libraries.md.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/RedExtension.cmake)

# The minimal extension, in C. The test suite loads it.
red_add_extension(example_ext ffi/example_ext.c)
set_target_properties(example_ext PROPERTIES C_STANDARD 11)

# The native half of lib/crc32.red, in C++.
red_add_extension(crc32_ext ffi/crc32_ext.cpp)

enable_testing()

# The runner is the interpreter itself, so `ctest` needs nothing beyond
# what was just built.
add_test(NAME conformance
  COMMAND $<TARGET_FILE:red> test ${CMAKE_CURRENT_SOURCE_DIR}/tests
)
add_test(NAME conformance-gc-stress
  COMMAND $<TARGET_FILE:red> test ${CMAKE_CURRENT_SOURCE_DIR}/tests --gc-stress
)
add_test(NAME conformance-compiled
  COMMAND $<TARGET_FILE:red> test ${CMAKE_CURRENT_SOURCE_DIR}/tests --compiled
)
