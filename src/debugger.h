// The interactive debugger behind `red debug`.
//
// The virtual machine calls Debugger::beforeInstruction() once per
// instruction, but only when runtime.debugger is set, so an ordinary run
// pays one predictable branch that is never taken.
//
// It stops on lines rather than on instructions, because a line is what
// the person reading the program wrote. Stepping into, over and out are
// the usual three, told apart by how deep the call stack is when the
// line changes.
#pragma once

#include <set>
#include <string>
#include <vector>

#include "value.h"

namespace red {

class VM;
struct CallFrame;

class Debugger {
 public:
  // `path` and `source` are the program being run, used to show the
  // lines around wherever it stops.
  Debugger(const std::string& path, const std::string& source);

  // Called by the dispatch loop. Cheap unless something is pending.
  void beforeInstruction(VM& vm, CallFrame* frame);

  // Called when a program fails, so that the state can be looked at
  // before everything is unwound. Returns true when the program should
  // keep going anyway.
  void onError(VM& vm, const std::string& message);

  // Prints the greeting and stops before the first line.
  void start();

 private:
  enum class Mode {
    Run,       // until a breakpoint
    StepLine,  // the next line, wherever it is
    StepOver,  // the next line at this depth or shallower
    StepOut,   // the next line shallower than this
  };

  std::string path_;
  std::vector<std::string> lines_;
  std::set<int> breakpoints_;
  Mode mode_ = Mode::StepLine;
  // Depth the current step started from, for over and out.
  int stepDepth_ = 0;
  int lastLine_ = -1;
  int lastDepth_ = -1;
  bool detached_ = false;

  // The prompt. Returns when the program should carry on.
  void prompt(VM& vm, CallFrame* frame, int line);
  void showLocation(VM& vm, CallFrame* frame, int line);
  void listAround(int line, int radius);
  void showBacktrace(VM& vm);
  void showLocals(VM& vm, CallFrame* frame, const std::string& only);
  bool lookUp(VM& vm, CallFrame* frame, const std::string& name, Value* out);
};

}  // namespace red
