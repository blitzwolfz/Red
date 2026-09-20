// Shared includes and small helpers used across the whole runtime.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <memory>

namespace red {

// Bumped whenever the bytecode layout changes in a way that breaks
// previously written chunks. docs/bytecode.md tracks the history.
constexpr int kBytecodeVersion = 1;

// Maximum call depth before the VM reports a stack overflow instead of
// smashing the host stack.
constexpr int kMaxFrames = 256;

// Size of one task's value stack. Each task allocates its own, so this is
// a per-thread cost and is kept well under a typical thread stack size.
constexpr int kMaxStack = 16384;

}  // namespace red
