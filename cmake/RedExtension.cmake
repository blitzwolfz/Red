# Building a Red extension.
#
# An extension is a shared library that Red loads with ffi_open(). It
# exports functions matching the contract in ffi/red_ffi.h (C) or
# ffi/red_ffi.hpp (C++), and links against nothing: the red_* helper
# functions are resolved in the interpreter itself when the library is
# loaded.
#
# In this repository the file is already included. From another project:
#
#   include(/path/to/red/cmake/RedExtension.cmake)
#   red_add_extension(mathx mathx.cpp)
#
# and the result is mathx.so in the build directory. Put that directory on
# RED_PATH, or copy the file next to the interpreter, and Red finds it by
# bare name. docs/libraries.md covers where it looks.

# Where red_ffi.h and red_ffi.hpp live. Derived from this file's own
# location, so including it from anywhere works.
get_filename_component(RED_FFI_INCLUDE_DIR
  "${CMAKE_CURRENT_LIST_DIR}/../ffi" ABSOLUTE)

# red_add_extension(<name> <source>...)
#
# Produces <name>.so. Every platform gets the same suffix, including
# macOS, so that Red code can name an extension without asking what it is
# running on.
function(red_add_extension name)
  if(NOT ARGN)
    message(FATAL_ERROR "red_add_extension(${name}) needs at least one source")
  endif()

  add_library(${name} MODULE ${ARGN})
  set_target_properties(${name} PROPERTIES
    PREFIX ""
    SUFFIX ".so"
    OUTPUT_NAME "${name}"
    POSITION_INDEPENDENT_CODE ON
  )
  target_include_directories(${name} PRIVATE ${RED_FFI_INCLUDE_DIR})

  if(APPLE)
    # The helpers live in the red binary, not in this module, so the
    # linker is told to leave them unresolved until load time. On Linux
    # that is already the default for a shared object.
    target_link_options(${name} PRIVATE -undefined dynamic_lookup)
  endif()
endfunction()
