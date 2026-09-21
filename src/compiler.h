// Single pass compiler: source goes straight to bytecode, with no AST in
// between. Expressions use Pratt parsing, statements use plain recursive
// descent. v1 built a tree and walked it; keeping the same parsing shape
// while changing the output is what made the rewrite tractable.
#pragma once

#include <string>

#include "common.h"
#include "object.h"
#include "runtime.h"

namespace red {

// Returns the module's top level function, or nullptr if the source did
// not compile. Errors are printed to stderr as they are found.
// When quiet is set, errors are not printed. The REPL uses that to try a
// line two ways without showing the reader a failed attempt.
ObjFunction* compile(Runtime& runtime, const std::string& source,
                     ObjModule* module, bool quiet = false);

}  // namespace red
