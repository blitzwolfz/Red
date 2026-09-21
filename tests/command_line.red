// The command line, and where a program says it is.
//
// This file is run by `red test`, which passes no arguments, so args()
// is empty here. What it checks is that the runner did not eat anything.
//
// It is not called cli.red, because a file next to the importing one
// wins over the library search path and tests/libraries.red imports
// lib/cli.red.

print(args().len());                      // expect: 0
print(type(args()));                      // expect: array

// source_path() and source_dir() answer for this file, wherever the
// program was started from.
// Compiled ahead of time this is a .redc, so only the stem is checked.
print(source_path().contains("command_line"));  // expect: true
print(source_dir().ends_with("tests"));       // expect: true
print(source_path().starts_with("/"));        // expect: true

// The library search path is where import and ffi_open look.
print(library_paths().len() >= 3);            // expect: true
print(type(library_paths()));                 // expect: array
