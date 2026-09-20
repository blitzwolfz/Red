// Every heap allocated Red value.
//
// All object types start with an Obj header. The header carries the type
// tag, the garbage collector's mark bit, and a next pointer. The runtime
// threads every live object onto one intrusive list through that next
// pointer, which is what the sweep phase walks. See docs/design.md.
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <thread>

#include "chunk.h"
#include "common.h"
#include "table.h"
#include "value.h"

namespace red {

class VM;
class Runtime;

enum class ObjType : uint8_t {
  String,
  Function,
  Native,
  Closure,
  Upvalue,
  Class,
  Instance,
  BoundMethod,
  Array,
  Map,
  Module,
  Channel,
  Task,
  File,
  Socket,
  NativeLib,
  Error,
};

struct Obj {
  ObjType type;
  bool isMarked;
  Obj* next;
};

inline bool isObjType(Value v, ObjType t) {
  return isObj(v) && asObj(v)->type == t;
}

struct ObjString {
  Obj obj;
  // Length is stored explicitly so strings may contain embedded nulls.
  size_t length;
  char* chars;
  uint32_t hash;
};

struct ObjModule {
  Obj obj;
  ObjString* name;
  ObjString* path;
  Table globals;
  // Set once the module body has finished running. An import that finds a
  // module already loading gets the partial module back rather than
  // looping, which is how import cycles terminate.
  bool loaded;
};

struct ObjFunction {
  Obj obj;
  int arity;
  int upvalueCount;
  // Upper bound on the value stack slots this function can use: its
  // locals plus the temporaries its expressions can hold. A call checks
  // this before pushing a frame, so a deep chain of wide frames reports
  // an overflow instead of running off the end of the stack.
  int slotCount;
  Chunk chunk;
  ObjString* name;
  ObjModule* module;
  // Type annotations are parsed and kept for the disassembler and for
  // error messages. Nothing checks them. This is deliberate, see the
  // non-goals in PRD.md.
  std::vector<std::string> paramTypes;
  std::string returnType;
};

using NativeFn = Value (*)(VM& vm, int argCount, Value* args);
// Calling convention for functions loaded from a shared library. It takes
// an opaque context instead of a VM reference so that ffi/red_ffi.h can
// describe it in plain C.
using ForeignFn = Value (*)(void* context, int argCount, Value* args);

struct ObjNative {
  Obj obj;
  NativeFn function;
  // Set when this native forwards to a symbol loaded through the FFI. It
  // takes priority over `function`.
  ForeignFn foreign;
  ObjString* name;
  // -1 means the function accepts any number of arguments.
  int arity;
};

struct ObjUpvalue {
  Obj obj;
  Value* location;
  Value closed;
  ObjUpvalue* next;
};

struct ObjClosure {
  Obj obj;
  ObjFunction* function;
  ObjUpvalue** upvalues;
  int upvalueCount;
};

struct ObjClass {
  Obj obj;
  ObjString* name;
  Table methods;
};

struct ObjInstance {
  Obj obj;
  ObjClass* klass;
  Table fields;
};

// Holds a receiver together with the thing to call on it. The callable is
// a Value rather than a closure so that the same type can bind a method
// written in Red and a method provided by the runtime.
struct ObjBoundMethod {
  Obj obj;
  Value receiver;
  Value method;
};

struct ObjArray {
  Obj obj;
  std::vector<Value> items;
};

struct ObjMap {
  Obj obj;
  ValueMap entries;
};

// A channel is a bounded queue guarded by the runtime lock. Waiting uses a
// condition variable over that same lock, so a blocked task releases the
// lock and lets other tasks and the collector run. docs/design.md covers
// why the lock is global.
struct ObjChannel {
  Obj obj;
  size_t capacity;
  std::deque<Value> buffer;
  bool closed;
  // Number of tasks currently parked on this channel. Used only to report
  // a clean error when every task would block forever.
  int waiters;
};

struct ObjTask {
  Obj obj;
  std::thread* thread;
  VM* vm;
  // The call to perform on the new thread. Held here rather than on a
  // stack so that it stays reachable between spawn and thread start.
  Value callee;
  std::vector<Value> args;
  Value result;
  bool done;
  bool failed;
  bool joined;
  ObjString* errorMessage;
};

struct ObjFile {
  Obj obj;
  FILE* handle;
  ObjString* path;
  bool open;
};

struct ObjSocket {
  Obj obj;
  int fd;
  bool listening;
  bool closed;
};

struct ObjNativeLib {
  Obj obj;
  void* handle;
  ObjString* path;
};

// The value produced by a runtime fault and by throw. Carrying a dedicated
// type rather than a plain instance keeps the VM's unwind path free of
// user visible class lookups.
struct ObjError {
  Obj obj;
  ObjString* message;
  // Formatted call stack captured where the error was raised.
  ObjString* trace;
  // Arbitrary user payload attached by throw.
  Value payload;
};

inline ObjString* asString(Value v) { return (ObjString*)asObj(v); }
inline ObjFunction* asFunction(Value v) { return (ObjFunction*)asObj(v); }
inline ObjNative* asNative(Value v) { return (ObjNative*)asObj(v); }
inline ObjClosure* asClosure(Value v) { return (ObjClosure*)asObj(v); }
inline ObjClass* asClass(Value v) { return (ObjClass*)asObj(v); }
inline ObjInstance* asInstance(Value v) { return (ObjInstance*)asObj(v); }
inline ObjBoundMethod* asBoundMethod(Value v) { return (ObjBoundMethod*)asObj(v); }
inline ObjArray* asArray(Value v) { return (ObjArray*)asObj(v); }
inline ObjMap* asMap(Value v) { return (ObjMap*)asObj(v); }
inline ObjModule* asModule(Value v) { return (ObjModule*)asObj(v); }
inline ObjChannel* asChannel(Value v) { return (ObjChannel*)asObj(v); }
inline ObjTask* asTask(Value v) { return (ObjTask*)asObj(v); }
inline ObjFile* asFile(Value v) { return (ObjFile*)asObj(v); }
inline ObjSocket* asSocket(Value v) { return (ObjSocket*)asObj(v); }
inline ObjNativeLib* asNativeLib(Value v) { return (ObjNativeLib*)asObj(v); }
inline ObjError* asError(Value v) { return (ObjError*)asObj(v); }

inline bool isString(Value v) { return isObjType(v, ObjType::String); }
inline bool isClosure(Value v) { return isObjType(v, ObjType::Closure); }
inline bool isClass(Value v) { return isObjType(v, ObjType::Class); }
inline bool isInstance(Value v) { return isObjType(v, ObjType::Instance); }
inline bool isArray(Value v) { return isObjType(v, ObjType::Array); }
inline bool isMap(Value v) { return isObjType(v, ObjType::Map); }
inline bool isChannel(Value v) { return isObjType(v, ObjType::Channel); }
inline bool isTask(Value v) { return isObjType(v, ObjType::Task); }
inline bool isFile(Value v) { return isObjType(v, ObjType::File); }
inline bool isSocket(Value v) { return isObjType(v, ObjType::Socket); }
inline bool isError(Value v) { return isObjType(v, ObjType::Error); }

std::string objectToString(Obj* obj, bool quoteStrings);
const char* objectTypeName(Obj* obj);

}  // namespace red
