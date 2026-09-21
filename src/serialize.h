// Reading and writing compiled programs.
//
// `red compile app.red` turns source into a .redc file, and running that
// file skips the compiler entirely. The format is described in
// docs/bytecode.md and carries the bytecode version, so a file built by
// one release is refused rather than misread by another.
#pragma once

#include <string>

#include "object.h"
#include "runtime.h"

namespace red {

// Every compiled file starts with these four bytes.
constexpr char kCompiledMagic[4] = {'R', 'E', 'D', 'C'};

// Does this look like a compiled file rather than source?
bool looksCompiled(const std::string& bytes);

// Writes a compiled program. Returns false and fills reason on failure.
bool writeCompiled(ObjFunction* root, std::string* out, std::string* reason);

// Reads one back, binding every function in it to `module`. Returns
// nullptr and fills reason on failure.
ObjFunction* readCompiled(Runtime& runtime, const std::string& bytes,
                          ObjModule* module, std::string* reason);

}  // namespace red
