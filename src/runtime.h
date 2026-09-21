// The Runtime owns everything that is shared between tasks: the heap, the
// string interner, the module cache, and the lock that serialises access
// to all of it.
//
// One Runtime exists per process. One VM exists per task.
#pragma once

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include "common.h"
#include "object.h"
#include "table.h"
#include "value.h"

namespace red {

class VM;

class Runtime {
 public:
  Runtime();
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // Held by whichever task is running bytecode. A task releases it before
  // any call that can block, and reacquires it afterwards. The collector
  // only ever runs while it is held, so a task that is not holding it has
  // a stack that is not moving and can be scanned safely.
  std::mutex lock;
  std::condition_variable cond;

  // ---- allocation -------------------------------------------------
  // Makes a string. Short ones are shared through the interner, because
  // a program that makes one has almost certainly made it before: single
  // characters from walking text, one and two character tokens. Longer
  // ones are allocated outright, because interning them costs a table
  // insert and a weak-table entry to save a comparison that content
  // equality already handles.
  ObjString* copyString(const char* chars, size_t length);
  ObjString* copyString(const std::string& text);
  // Always shared. For names the runtime and the compiler look up over
  // and over: identifiers, method names, module paths.
  ObjString* internString(const std::string& text);
  ObjString* internString(const char* chars, size_t length);
  // Takes ownership of a buffer that was allocated with new char[].
  ObjString* takeString(char* chars, size_t length);


  ObjFunction* newFunction(ObjModule* module);
  ObjNative* newNative(NativeFn fn, const std::string& name, int arity);
  ObjClosure* newClosure(ObjFunction* function);
  ObjUpvalue* newUpvalue(Value* slot);
  ObjClass* newClass(ObjString* name);
  ObjInstance* newInstance(ObjClass* klass);
  ObjBoundMethod* newBoundMethod(Value receiver, Value method);
  ObjArray* newArray();
  ObjMap* newMap();
  ObjSet* newSet();
  ObjModule* newModule(ObjString* name, ObjString* path);
  ObjEnum* newEnum(ObjString* name);
  ObjEnumMember* newEnumMember(ObjEnum* parent, ObjString* name, double value);
  ObjChannel* newChannel(size_t capacity);
  ObjTask* newTask();
  ObjFile* newFile(FILE* handle, ObjString* path);
  ObjSocket* newSocket(int fd, bool listening);
  // Takes ownership of the compiled program.
  ObjRegex* newRegex(Regex* program);
  ObjNativeLib* newNativeLib(void* handle, ObjString* path);
  ObjError* newError(ObjString* message, ObjString* trace, Value payload,
                     ObjString* kind);

  // ---- garbage collection -----------------------------------------
  void collectGarbage();
  void maybeCollect();
  void markObject(Obj* obj);
  void markValue(Value value);

  // Objects that are reachable only from a C++ local while they are being
  // built. Use the GCRoot guard rather than calling these directly.
  void pushRoot(Obj* obj);
  void popRoot();

  void registerVM(VM* vm);
  void unregisterVM(VM* vm);

  // A running task is a root even when no live value refers to it. Its
  // thread is still using the task object, so the collector must not free
  // it. The entry is dropped once the task finishes and is joined.
  void registerTask(ObjTask* task);
  void retireTask(ObjTask* task);
  // Waits for every task that was never joined. Called once at shutdown,
  // by a thread that does not hold the runtime lock.
  void joinAllTasks();

  // ---- shared state -----------------------------------------------
  Table strings;   // interner, weak: entries die with their string
  Table modules;   // absolute path -> ObjModule
  // Names visible from every module. A global lookup checks the current
  // module first and falls back to this table.
  Table builtins;
  std::vector<std::string> scriptArgs;
  ObjModule* mainModule = nullptr;
  // Set when this process is a program built with `red build`. Imports
  // are then answered out of the executable rather than the file system,
  // so a built program carries its libraries with it.
  const struct Bundle* bundle = nullptr;

  // Interned names the VM needs on hot paths.
  ObjString* initString = nullptr;
  ObjString* messageString = nullptr;
  // The two methods a class may define to say how its instances print
  // and compare. Interned once so that looking for them costs a pointer
  // comparison rather than a hash of the name.
  ObjString* strString = nullptr;
  ObjString* eqString = nullptr;
  // The kind given to an error raised by the runtime when nothing more
  // specific fits.
  ObjString* runtimeKind = nullptr;

  // Tables owned by the standard library that hold live objects. They are
  // registered here so the collector can treat them as roots without the
  // core runtime knowing what is in them.
  std::vector<Table*> rootTables;

  // Set by `red debug`. The dispatch loop checks it once an instruction,
  // which costs one branch that is never taken in an ordinary run.
  class Debugger* debugger = nullptr;
  bool traceExecution = false;
  bool logGC = false;
  bool stressGC = false;

  size_t bytesAllocated = 0;
  size_t nextGC = 1024 * 1024;
  size_t collections = 0;
  size_t peakBytes = 0;

 private:
  // Builds the object around a buffer this runtime now owns, and puts it
  // in the interner when `share` says to.
  ObjString* allocateString(char* chars, size_t length, uint32_t hash,
                            bool share);

  Obj* objects_ = nullptr;
  std::vector<Obj*> grayStack_;
  std::vector<Obj*> tempRoots_;
  std::vector<VM*> vms_;
  std::vector<ObjTask*> liveTasks_;
  // Set while the collector is running so that allocations made by the
  // collector itself cannot re-enter it.
  bool collecting_ = false;

  void markRoots();
  void traceReferences();
  void blackenObject(Obj* obj);
  void sweep();
  void freeObject(Obj* obj);

  friend class VM;
};

// Pins an object for as long as the guard is alive. Needed whenever a
// native or a runtime helper holds a fresh object in a C++ local and then
// allocates again before storing it somewhere the collector can see.
class GCRoot {
 public:
  GCRoot(Runtime& rt, Obj* obj) : rt_(rt) { rt_.pushRoot(obj); }
  GCRoot(Runtime& rt, Value v) : rt_(rt) {
    rt_.pushRoot(isObj(v) ? asObj(v) : nullptr);
  }
  ~GCRoot() { rt_.popRoot(); }
  GCRoot(const GCRoot&) = delete;
  GCRoot& operator=(const GCRoot&) = delete;

 private:
  Runtime& rt_;
};

}  // namespace red
