// The Runtime owns everything that is shared between tasks: the heap, the
// string interner and the module cache.
//
// One Runtime exists per process. One VM exists per task, on its own
// thread, and they run at the same time. Nothing serialises them while
// they are executing bytecode.
//
// What that costs is described in docs/design.md under Concurrency. In
// short: allocation is thread local, the collector stops the world at
// safepoints the threads poll, and the shared tables are guarded. A
// program that never spawns a task pays for none of it, because the
// guards are skipped until a second thread exists.
#pragma once

#include <atomic>
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

// One per thread that runs Red code.
//
// The heap is shared, but each thread allocates onto a list of its own,
// so making an object needs no agreement with anyone. The collector
// walks every list once the world has stopped.
struct Thread {
  Obj* objects = nullptr;
  // What this thread has allocated and not yet folded into the shared
  // total, and what it has allocated altogether.
  size_t bytesAllocated = 0;
  size_t sinceRollup = 0;
  // Objects held only by a C++ local. See GCRoot.
  std::vector<Obj*> tempRoots;
  VM* vm = nullptr;
  // False while this thread is running bytecode and its stack is
  // moving. True where the collector may scan it.
  std::atomic<bool> parked{false};
  // Where this entry sits in the runtime's list. Kept up to date so that
  // leaving costs a swap rather than a search: a program with a hundred
  // thousand fibers has a hundred thousand entries, and searching each
  // one out on the way past is the difference between linear and
  // quadratic.
  size_t index = 0;
};

class Runtime {
 public:
  Runtime();
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // Woken when a task finishes, for join().
  std::mutex lock;
  std::condition_variable cond;

  // ---- threads and safepoints -------------------------------------
  //
  // The collector needs every thread's stack to be still while it
  // scans. Rather than a lock held across all of execution, each thread
  // parks itself at a point where its stack is in a state the collector
  // understands, and the collector waits for all of them.
  //
  // A thread parks at a safepoint, which it polls at every allocation,
  // every backward jump and every call, so a running thread always
  // reaches one soon. It also parks around anything that blocks, since
  // a thread waiting on a socket has a stack that is not moving anyway.

  // Registers the calling thread. Its objects, its temporary roots and
  // its parked flag live in the entry this returns.
  Thread* attachThread(VM* vm);
  // Makes an entry for a thread that has not started yet, from the
  // thread that is about to start it. Registering it before the new
  // thread runs is what stops a collection from beginning in the gap
  // and finding a thread it does not know about.
  Thread* newThread();
  // Called by that thread once it is running, with its VM.
  void adoptThread(Thread* thread, VM* vm);
  void detachThread(Thread* thread);
  // Takes a VM off a thread that outlives it, under the same lock the
  // collector's view of the threads is kept consistent by.
  void clearThreadVM(Thread* thread, VM* vm);
  // The calling thread's entry.
  static Thread* currentThread();
  // Rebinds what the calling thread counts as its entry.
  //
  // The scheduler calls this on every fiber switch. One operating system
  // thread runs many fibers, each with an entry of its own, so which one
  // is current changes without the thread changing.
  void setCurrentThread(Thread* thread);
  // The runtime attached to the calling thread. This is primarily for
  // helpers such as the value printer that receive an object but not a
  // VM and still need its aggregate guard.
  static Runtime* current();

  // Parks if the collector is waiting. Cheap enough for a hot loop: one
  // relaxed load of a flag that is almost never set.
  void safepoint() {
    if (gcPending.load(std::memory_order_relaxed)) reachSafepoint();
  }
  // Parks for the duration of something that blocks. The caller must not
  // touch the heap in between, because the collector may run.
  void park();
  void unpark();

  // Takes a mutex that another thread may be holding while parked for a
  // collection.
  //
  // Waiting for such a mutex without parking is what deadlocks: the
  // collector waits for this thread to reach a safepoint, this thread
  // waits for a mutex held by a thread that is already parked, and the
  // collection that would release it never starts. Parking first makes
  // this thread one the collector can go ahead without.
  void lockParked(std::mutex& mutex);

  // True once a second thread has run Red code. It is set before the
  // first task's thread starts and never cleared, so the guards that
  // read it never need to be more than a relaxed load.
  //
  // This is what keeps an ordinary program free of the cost of being
  // able to run in parallel.
  static bool parallel() { return runningInParallel(); }
  static void becomeParallel();

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
  // Counts memory that hangs off an object rather than being the object:
  // a string's characters, a closure's upvalue array. It goes on the
  // allocating thread's total, because that is the total the sweep takes
  // it off again.
  void noteBytes(size_t bytes);

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
  // A type. `parts` is moved in, so a caller that built them into a
  // vector has to keep those rooted until this returns.
  ObjTypeDesc* newTypeDesc(TypeKind kind, ObjString* name,
                           std::vector<ObjTypeDesc*> parts);

  // ---- garbage collection -----------------------------------------
  void collectGarbage();
  void maybeCollect();
  void markObject(Obj* obj);
  void markValue(Value value);

  // Objects that are reachable only from a C++ local while they are being
  // built. Use the GCRoot guard rather than calling these directly.
  void pushRoot(Obj* obj);
  void popRoot();

  // A running task is a root even when no live value refers to it. Its
  // thread is still using the task object, so the collector must not free
  // it. The entry is dropped once the task finishes and is joined.
  void registerTask(ObjTask* task);
  void retireTask(ObjTask* task);
  // Waits for every task that was never joined. Called once at shutdown.
  void joinAllTasks();
  // Joins one task's thread, if nobody else has. Safe to call from
  // several places at once, and from the collector.
  void reapTask(ObjTask* task);

  // ---- shared state -----------------------------------------------
  // The interner. Weak: entries die with their string. Guarded by
  // internMutex_ rather than by the seqlock every other table uses,
  // because looking a string up and putting it there have to be one
  // step: two threads that each made a string for the same text would
  // break the pointer comparison that keys everything else.
  Table strings;
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
  // `await` joins through the same method a program would call, so that
  // one of them cannot come to mean something the other does not.
  ObjString* joinString = nullptr;
  // The two methods a class may define to say how its instances print
  // and compare. Interned once so that looking for them costs a pointer
  // comparison rather than a hash of the name.
  ObjString* strString = nullptr;
  ObjString* eqString = nullptr;
  // The kind given to an error raised by the runtime when nothing more
  // specific fits.
  ObjString* runtimeKind = nullptr;

  // One object per type that takes no parameters, indexed by TypeKind, so
  // that every `Num` written anywhere is the same object. Named,
  // Optional and the parameterised containers are not in here, since
  // those differ by what is inside them.
  std::vector<ObjTypeDesc*> simpleTypes;

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

  // Totalled across the threads. Each thread counts what it allocates
  // and folds the total in occasionally, so an allocation costs a plain
  // add rather than an atomic one.
  std::atomic<size_t> bytesAllocated{0};
  size_t nextGC = 1024 * 1024;
  size_t collections = 0;
  size_t peakBytes = 0;

 private:
  // Guards the interner for the length of a lookup and the insert that
  // follows it.
  class InternLock {
   public:
    explicit InternLock(Runtime& rt);
    ~InternLock();
    InternLock(const InternLock&) = delete;
    InternLock& operator=(const InternLock&) = delete;

   private:
    Runtime& rt_;
    bool held_ = false;
  };

  // Set while a collection is waiting for the threads to park.
  std::atomic<bool> gcPending{false};
  // Set, under worldMutex_, while stopWorld() is in its wait loop.
  //
  // A thread that parks stores its flag and then reads this; stopWorld
  // stores this and then reads the flags. Both pairs are sequentially
  // consistent, so at least one of them sees the other: either the
  // parking thread finds a collector to notify and takes worldMutex_ to
  // do it, which cannot overlap the collector's own look at the list, or
  // the collector's look already includes that thread as parked. It is
  // the same argument two threads deciding who goes first have always
  // used, and it means the notify costs one relaxed load on the path
  // where nobody is collecting.
  std::atomic<bool> worldWaiting_{false};
  std::mutex internMutex_;
  std::mutex worldMutex_;
  std::condition_variable worldCond_;
  std::vector<Thread*> threads_;

  void reachSafepoint();
  // Tells a collector that is waiting for the world to stop that this
  // thread has parked.
  //
  // The signal is a condition variable, and the three places that send
  // it park a thread and then notify without holding worldMutex_, on
  // purpose: taking a global lock on every park would serialise every
  // worker, and parking is what a fiber does on every switch. The cost
  // of not taking it is that a notify landing between the collector's
  // look at the thread list and its wait() is delivered to nobody, and
  // the collector then waits for a thread that has already parked.
  //
  // worldWaiting_ closes that window. See the note on it.
  void announceParked();
  // Parks the caller and waits for every other thread to park. The
  // caller holds worldMutex_ and keeps holding it, so the world stays
  // stopped until startWorld() lets it go.
  void stopWorld(std::unique_lock<std::mutex>& guard);
  void startWorld();

  // Builds the object around a buffer this runtime now owns, and puts it
  // in the interner when `share` says to.
  ObjString* allocateString(char* chars, size_t length, uint32_t hash,
                            bool share);

  std::vector<Obj*> grayStack_;
  // Guards liveTasks_. Held only for the moment it takes to add or drop
  // an entry, and never while anything that can allocate runs, so the
  // collector can take it too.
  std::mutex taskListMutex_;
  std::vector<ObjTask*> liveTasks_;
  Thread* mainThread_ = nullptr;
  // Set while the collector is running so that allocations made by the
  // collector itself cannot re-enter it.
  bool collecting_ = false;

  void markRoots();
  void traceReferences();
  void blackenObject(Obj* obj);
  void sweep();
  // Returns how many bytes it gave back.
  size_t freeObject(Obj* obj);

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
