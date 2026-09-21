// Shared includes and small helpers used across the whole runtime.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>
#include <memory>

namespace red {

// Bumped whenever the bytecode layout changes in a way that breaks
// previously written chunks. docs/bytecode.md tracks the history.
constexpr int kBytecodeVersion = 4;

// Maximum call depth before the VM reports a stack overflow instead of
// smashing the host stack. A recursive descent parser written in Red
// needs more than a few hundred, and the frame table costs 32 bytes an
// entry, so this is set well above what ordinary code uses.
constexpr int kMaxFrames = 1024;

// Size of one task's value stack, in slots. Each task allocates its own,
// so this is a per-thread cost: 64K slots is one megabyte per task.
//
// The frame limit alone does not bound stack use, because one frame can
// hold up to 256 locals plus its temporaries. Calls check the callee's
// slotCount against what is left, which is what actually keeps pushes
// inside this buffer.
constexpr int kMaxStack = 65536;

// True once more than one thread runs Red code. It is set before the
// first task's thread starts and never cleared, so a relaxed load is all
// any reader needs.
//
// Everything that has to agree between threads is guarded, and every one
// of those guards is skipped while this is false. That is what keeps a
// program with no tasks in it from paying for the ability to have them:
// a lookup in a table costs one predictable branch more than it did, and
// nothing else.
extern std::atomic<bool> gParallel;
inline bool runningInParallel() {
  return gParallel.load(std::memory_order_relaxed);
}

}  // namespace red
