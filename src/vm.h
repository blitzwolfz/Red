// The stack machine that executes compiled chunks.
//
// One VM per task. Every VM shares a Runtime, and only the VM currently
// holding the runtime lock may touch the heap.
#pragma once

#include <mutex>
#include <string>

#include "common.h"
#include "object.h"
#include "runtime.h"
#include "value.h"

namespace red {

struct CallFrame {
  ObjClosure* closure;
  uint8_t* ip;
  Value* slots;
  // How many arguments the call site actually supplied, before any
  // padding. The prologue uses it to decide whether a parameter needs its
  // default value.
  int argCount;
};

// One entry per active try block. The VM keeps these in a flat stack so
// that unwinding is a truncation rather than a walk.
struct TryHandler {
  int frameIndex;
  ptrdiff_t stackOffset;
  uint8_t* catchIp;
};

enum class InterpretResult {
  Ok,
  CompileError,
  RuntimeError,
};

class VM {
 public:
  explicit VM(Runtime& runtime);
  ~VM();
  VM(const VM&) = delete;
  VM& operator=(const VM&) = delete;

  // Compiles source into module and runs it. The caller must already hold
  // the runtime lock.
  InterpretResult interpret(const std::string& source, ObjModule* module);
  // Runs an already compiled top level function.
  InterpretResult runFunction(ObjFunction* function);
  // Calls a callable value. The caller must already have pushed the
  // callee and then its arguments, exactly as a compiled call site does.
  // On return the stack is back to how it was before the callee was
  // pushed, and the result is in *result.
  InterpretResult callAndRun(Value callee, int argCount, Value* result);

  void push(Value value);
  Value pop();
  Value peek(int distance) const;

  Runtime& runtime() { return runtime_; }
  ObjModule* currentModule();

  // For the debugger: how deep the call stack is, and the frame on top.
  int frameDepth() const { return frameCount_; }
  CallFrame* topFrame() {
    return frameCount_ == 0 ? nullptr : &frames_[frameCount_ - 1];
  }

  // Turns a value into text the way print does. A class may define a
  // str() method of its own, which is why this needs the VM and the
  // plain valueToString() does not. Containers are walked, so a class
  // with a str() prints through it inside an array too.
  std::string stringify(Value value);
  // The same, with strings quoted, as repr() shows them and as a
  // container shows the values inside it.
  std::string display(Value value);
  // Compares two values. When both are instances of a class that defines
  // eq(), that method decides; otherwise this is valuesEqual().
  bool equal(Value a, Value b);

  // Reports a failure from inside a native function. Always returns nil so
  // a native can write `return vm.fail("...")`.
  Value fail(const char* format, ...);
  // The same, tagged with a kind that a catch clause can select on.
  Value failAs(const char* kind, const char* format, ...);
  // Builds an error value without raising it. Natives that construct an
  // error for the program to inspect use this.
  Value makeError(const char* kind, const std::string& message, Value payload);
  // Reports an error that already exists, keeping its kind, payload and
  // trace. join() uses this to raise what the task raised rather than a
  // description of it.
  Value reraise(Value error);
  bool failed() const { return failed_; }

  // Formats the active call stack, innermost frame first.
  std::string buildTrace();

 private:
  // Shared body of stringify() and display(). depth bounds the walk the
  // same way the plain printer does, because a container can hold
  // itself.
  std::string stringifyAt(Value value, bool quoteStrings, int depth);
  // The named method of value's class, or nil when there is no such
  // class or no such method.
  Value classHook(Value value, ObjString* name);

 public:

  // The lock guard this task holds while running. Natives that block must
  // release it, and must not touch the heap while it is released.
  std::unique_lock<std::mutex>& lock() { return lock_; }
  void acquireLock();
  void releaseLock();
  // Takes the lock and joins the collector's root set. Called once, when
  // the task that owns this VM starts.
  void attach();
  // Leaves the root set and drops the lock. Called once, at task exit.
  void detach();

  // Roots the collector needs from this task.
  void markRoots();

  Value lastError = nilValue();
  // Set when a task's body finished with an unhandled error.
  bool taskFailed = false;

 private:
  Runtime& runtime_;
  std::unique_lock<std::mutex> lock_;

  Value* stack_ = nullptr;
  Value* stackTop_ = nullptr;
  CallFrame frames_[kMaxFrames];
  int frameCount_ = 0;
  std::vector<TryHandler> handlers_;
  ObjUpvalue* openUpvalues_ = nullptr;

  bool failed_ = false;
  Value failValue_ = nilValue();
  // Frames below this index belong to an outer run loop. Unwinding never
  // crosses it, which is what keeps a nested import from stealing the
  // enclosing program's catch blocks.
  int baseFrame_ = 0;

  InterpretResult run(int baseFrame);
  // The dispatch loop. Instantiated twice: once with the debugger and
  // tracer compiled in, and once with them compiled out entirely. A
  // single `if` at the top of the loop, even on a flag held in a
  // register, cost 30% on a tight loop, because it is one more basic
  // block in the place where there should be none.
  template <bool Instrumented>
  InterpretResult runLoop(int baseFrame);

  bool call(ObjClosure* closure, int argCount);
  bool callValue(Value callee, int argCount);
  bool invoke(ObjString* name, int argCount);
  bool invokeFromClass(ObjClass* klass, ObjString* name, int argCount);
  bool bindMethod(ObjClass* klass, ObjString* name);
  bool getBuiltinProperty(Value receiver, ObjString* name);
  ObjUpvalue* captureUpvalue(Value* local);
  void closeUpvalues(Value* last);
  void defineMethod(ObjString* name);

  bool binaryNumeric(uint8_t op);
  bool concatenate();
  bool getIndex();
  bool setIndex();
  bool spawnTask(int argCount);
  bool importModule(ObjString* path);

  // Builds an error value and hands it to raise().
  bool runtimeError(const char* format, ...);
  bool runtimeErrorAs(const char* kind, const char* format, ...);
  // Finds a handler for value. Returns true when execution can continue at
  // a catch block, false when the error escapes this VM.
  bool raise(Value value);

  friend class Runtime;
  friend void markVMRoots(VM* vm);
};

}  // namespace red
