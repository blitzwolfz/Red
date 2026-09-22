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
#include "regex.h"
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
  Set,
  Module,
  Enum,
  EnumMember,
  Channel,
  Task,
  File,
  Socket,
  Regex,
  NativeLib,
  Error,
  // Added after the first release, so it goes on the end: the method
  // tables in stdlib/registry.cpp are indexed by these numbers.
  Type,
};

struct Obj {
  ObjType type;
  bool isMarked;
  // How many times the thread in `owner` has taken this object's lock
  // without letting go. See ObjLock below. These two sit in padding the
  // header already had, so an object is no larger than it was.
  uint8_t lockDepth;
  uint8_t unused;
  std::atomic<uint32_t> owner;
  Obj* next;
};
static_assert(sizeof(Obj) == 16, "the object header should stay this size");

// The number this thread puts in Obj::owner. Zero means no thread, so
// the numbering starts at one.
uint32_t currentThreadId();

// Holds an aggregate still for as long as the guard lives, so that one
// thread walking an array cannot see it part way through another
// thread's push.
//
// Nothing at all happens until a second thread exists: an ordinary
// program pays one predictable branch per operation.
//
// The lock is reentrant, because a Red callback inside sort() or map()
// can reach the same array. Waiting for one another thread holds parks
// this one now and then, so a thread that is waiting never keeps the
// collector waiting.
class ObjLock {
 public:
  // Uses the runtime attached to this operating-system thread. This is
  // for code that only has a Value, such as the general-purpose printer.
  explicit ObjLock(Obj* object);
  ObjLock(class Runtime& runtime, Obj* object);
  ObjLock(class Runtime& runtime, Value value);
  // Two aggregates at once, taken in address order so that two threads
  // doing the same thing from opposite sides cannot each hold what the
  // other wants.
  ObjLock(class Runtime& runtime, Obj* first, Obj* second);
  ~ObjLock();
  ObjLock(const ObjLock&) = delete;
  ObjLock& operator=(const ObjLock&) = delete;

 private:
  void take(Obj* object);
  void release(Obj* object);

  class Runtime* runtime_ = nullptr;
  Obj* held_[2] = {nullptr, nullptr};
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

// One local variable's name, and the stretch of bytecode over which that
// slot holds it. Slots are reused between scopes, so a slot number alone
// does not name anything.
struct LocalName {
  std::string name;
  int slot;
  int start;
  int end;
};

struct ObjFunction {
  Obj obj;
  // Parameters that must be supplied.
  int arity;
  // Parameters declared, including those with defaults. A call pads the
  // missing ones with nil and lets the prologue fill them in.
  int maxArity;
  // True when the last parameter was written with "...", which gathers
  // any further arguments into an array.
  bool hasRest;
  int upvalueCount;
  // Upper bound on the value stack slots this function can use: its
  // locals plus the temporaries its expressions can hold. A call checks
  // this before pushing a frame, so a deep chain of wide frames reports
  // an overflow instead of running off the end of the stack.
  int slotCount;
  Chunk chunk;
  ObjString* name;
  ObjModule* module;
  // What the parameters were called and what they were declared to be,
  // one entry each, in order. A rest parameter has an empty type.
  //
  // Both are part of the function's interface rather than debug
  // information, so both survive into a compiled file: an annotation
  // that fails should name the parameter the same way whether the
  // program was run from source or from a .redc.
  std::vector<std::string> paramNames;
  std::vector<std::string> paramTypes;
  std::string returnType;
  // Where each local lives and what it was called, for `red debug`.
  // Filled in only when a function is compiled from source: a .redc does
  // not carry it, because names are for people and the format is for the
  // machine. Empty means the debugger shows slot numbers.
  std::vector<LocalName> localNames;
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
  // Where the captured variable lives: a slot on the stack of the task
  // that made the closure, until that slot dies and the value moves into
  // `closed` below.
  //
  // Atomic because a closure can be handed to another task, which then
  // reads the variable through this while the task that made it is
  // moving the variable off its stack. The move publishes `closed` and
  // then this pointer, so a reader that sees the new pointer sees the
  // value that went with it.
  std::atomic<Value*> location;
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
  // Kept so that a value can be tested against a whole hierarchy. Method
  // lookup does not use it, because methods are copied down at the point
  // of inheritance.
  ObjClass* superclass;
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

// A set of distinct values. It reuses the map table with the values
// ignored, so membership follows exactly the same rules as map keys.
struct ObjSet {
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
  // Number of tasks currently parked on this channel, of either kind.
  int waiters;
  // Fibers parked on this channel, waiting to send and waiting to
  // receive. A task running on an operating system thread instead waits
  // on the runtime's condition variable, so both lists can be empty
  // while tasks are still waiting.
  std::vector<struct Fiber*> sendWaiters;
  std::vector<struct Fiber*> recvWaiters;
};

struct ObjTask {
  Obj obj;
  // Set only for a task running on an operating system thread of its
  // own, which is what a task was before the scheduler existed and what
  // one still is outside it: in the REPL, the debugger and the test
  // runner.
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
  // Set by whichever of join(), the shutdown sweep or the collector
  // gets to the operating system thread first. Exchanged rather than
  // read and written, because all three can be looking at once and
  // joining a thread twice is not allowed.
  std::atomic<bool> reaped;
  // What the task failed with, kept whole so that join() can raise the
  // same error rather than a description of one. Nil when it succeeded.
  Value error;
  // Where this task sits in the runtime's list of live tasks, so that
  // finishing one costs a swap rather than a search through every task
  // a program has running.
  long liveIndex;
  // Fibers parked in join() on this task. Woken, under the task mutex,
  // by the fiber that finishes it. Empty for a task nobody is waiting
  // on, which is the common case.
  std::vector<struct Fiber*> waiters;
};

struct ObjFile {
  Obj obj;
  FILE* handle;
  ObjString* path;
  bool open;
};

struct ObjSocket {
  Obj obj;
  // Atomic because a socket is reachable from more than one task, and
  // closing one is something a task does to a socket another task is
  // reading. The close itself is a pair of exchanges, so exactly one
  // caller closes the descriptor however many ask.
  std::atomic<int> fd;
  bool listening;
  std::atomic<bool> closed;
  // How long a read, a write, an accept or a connect may wait before
  // giving up. Zero means forever, which is the default.
  double timeout;
};

struct ObjNativeLib {
  Obj obj;
  void* handle;
  ObjString* path;
};

// A compiled regular expression. The program inside is built once, when
// the pattern is compiled, and reused by every match against it.
struct ObjRegex {
  Obj obj;
  Regex* program;
};

// A set of named constants. Members are built once, when the enum is
// declared, and every mention of one yields the same object, so they
// compare by identity and can be used as map keys.
struct ObjEnum {
  Obj obj;
  ObjString* name;
  // Name to member.
  Table members;
  // Members in declaration order, which is what values() reports.
  std::vector<Value> ordered;
};

struct ObjEnumMember {
  Obj obj;
  ObjEnum* parent;
  ObjString* name;
  double value;
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
  // What sort of failure this is, so that a catch clause can select. The
  // runtime uses a fixed set of names, listed in docs/language.md. A
  // thrown class instance takes that class's name.
  ObjString* kind;
};

// What a type describes. The order is not written to disk, so it is free
// to change; the serializer writes the name.
enum class TypeKind : uint8_t {
  // Matches anything, and compiles to no check at all.
  Any,
  Nil,
  Bool,
  Num,
  // A number with nothing after the point. Red has one number type, so
  // this is a question about the value rather than about its
  // representation.
  Int,
  String,
  Array,
  Map,
  Set,
  Fun,
  Error,
  // A class or an enum, named. Which one is not known until the program
  // runs, because a type annotation can be written above the class it
  // names.
  Named,
  // `T?`: the inner type, or nil.
  Optional,
};

struct ObjTypeDesc {
  Obj obj;
  TypeKind kind;
  // Named only.
  ObjString* name;
  // Array: the element type. Map: the key type then the value type.
  // Set: the element type. Optional: the inner type. Fun: each parameter
  // in order, then the return type last. Empty when the type was written
  // without parameters, which makes it a question about the kind alone.
  std::vector<ObjTypeDesc*> parts;
  // What a Named type turned out to mean. A name binds once, so the
  // answer is kept rather than looked up on every check.
  Value resolved;
  bool didResolve;
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
inline ObjSet* asSet(Value v) { return (ObjSet*)asObj(v); }
inline ObjModule* asModule(Value v) { return (ObjModule*)asObj(v); }
inline ObjChannel* asChannel(Value v) { return (ObjChannel*)asObj(v); }
inline ObjTask* asTask(Value v) { return (ObjTask*)asObj(v); }
inline ObjFile* asFile(Value v) { return (ObjFile*)asObj(v); }
inline ObjSocket* asSocket(Value v) { return (ObjSocket*)asObj(v); }
inline ObjRegex* asRegex(Value v) { return (ObjRegex*)asObj(v); }
inline ObjNativeLib* asNativeLib(Value v) { return (ObjNativeLib*)asObj(v); }
inline ObjEnum* asEnum(Value v) { return (ObjEnum*)asObj(v); }
inline ObjEnumMember* asEnumMember(Value v) {
  return (ObjEnumMember*)asObj(v);
}
inline ObjError* asError(Value v) { return (ObjError*)asObj(v); }
inline ObjTypeDesc* asTypeDesc(Value v) { return (ObjTypeDesc*)asObj(v); }

inline bool isString(Value v) { return isObjType(v, ObjType::String); }
inline bool isClosure(Value v) { return isObjType(v, ObjType::Closure); }
inline bool isClass(Value v) { return isObjType(v, ObjType::Class); }
inline bool isInstance(Value v) { return isObjType(v, ObjType::Instance); }
inline bool isArray(Value v) { return isObjType(v, ObjType::Array); }
inline bool isMap(Value v) { return isObjType(v, ObjType::Map); }
inline bool isSet(Value v) { return isObjType(v, ObjType::Set); }
inline bool isChannel(Value v) { return isObjType(v, ObjType::Channel); }
inline bool isTask(Value v) { return isObjType(v, ObjType::Task); }
inline bool isFile(Value v) { return isObjType(v, ObjType::File); }
inline bool isSocket(Value v) { return isObjType(v, ObjType::Socket); }
inline bool isRegex(Value v) { return isObjType(v, ObjType::Regex); }
inline bool isEnum(Value v) { return isObjType(v, ObjType::Enum); }
inline bool isEnumMember(Value v) { return isObjType(v, ObjType::EnumMember); }
inline bool isError(Value v) { return isObjType(v, ObjType::Error); }
inline bool isTypeDesc(Value v) { return isObjType(v, ObjType::Type); }

// Anything a call expression can be applied to. A class counts, because
// calling one builds an instance.
inline bool isCallable(Value v) {
  if (!isObj(v)) return false;
  switch (asObj(v)->type) {
    case ObjType::Closure:
    case ObjType::Native:
    case ObjType::Class:
    case ObjType::BoundMethod:
      return true;
    default:
      return false;
  }
}

// Values allowed as map keys. A number, a bool, nil, a string or an enum
// member compares by value. An instance compares by identity: two
// separately built objects with the same fields are two keys, the same
// way they are two objects. A class that wants its instances to key by
// value gives them a method that returns a string or a number, and the
// program uses that as the key.
//
// An array, a map or a set is not a key. They can be changed after they
// are used as one, which would leave the entry somewhere the table can
// no longer find it.
inline bool isHashableKey(Value v) {
  return !isObj(v) || isString(v) || isEnumMember(v) || isInstance(v);
}

std::string objectToString(Obj* obj, bool quoteStrings);
const char* objectTypeName(Obj* obj);

}  // namespace red
