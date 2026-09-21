#include "runtime.h"

#include <cassert>

#include <cstdio>
#include <cstdlib>
#include <unistd.h>

#include "vm.h"

namespace red {

namespace {

// FNV-1a. Cheap, good enough spread for identifier sized keys, and small
// enough to read in one sitting.
uint32_t hashString(const char* key, size_t length) {
  uint32_t hash = 2166136261u;
  for (size_t i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619u;
  }
  return hash;
}

// Heap grows by this factor between collections. A larger factor trades
// memory for fewer pauses.
constexpr size_t kHeapGrowFactor = 2;

// How much a thread may allocate before folding its count into the
// shared total. Small enough that the trigger is not missed by much,
// large enough that the atomic add is rare.
constexpr size_t kRollupBytes = 64 * 1024;

void markTable(Runtime& rt, Table& table) {
  for (int i = 0; i < table.capacity(); i++) {
    Entry* entry = &table.entries()[i];
    if (entry->key != nullptr) rt.markObject((Obj*)entry->key);
    rt.markValue(entry->value);
  }
}

}  // namespace

// One Runtime per process, so the calling thread's entry can be a
// thread local rather than something every allocation has to be handed.
thread_local Thread* t_thread = nullptr;
thread_local Runtime* t_runtime = nullptr;

std::atomic<bool> gParallel{false};

Thread* Runtime::currentThread() { return t_thread; }

void Runtime::setCurrentThread(Thread* thread) {
  t_thread = thread;
  t_runtime = this;
}

Runtime* Runtime::current() { return t_runtime; }

// Takes the interner's lock, and only bothers once a second thread
// exists.
Runtime::InternLock::InternLock(Runtime& rt) : rt_(rt) {
  if (!runningInParallel()) return;
  held_ = true;
  // Parks while waiting: allocating a string can start a collection, so
  // a thread that holds this can be parked when another one asks for it.
  rt_.lockParked(rt_.internMutex_);
}

Runtime::InternLock::~InternLock() {
  if (held_) rt_.internMutex_.unlock();
}

void Runtime::noteBytes(size_t bytes) {
  if (t_thread == nullptr) return;
  t_thread->bytesAllocated += bytes;
  t_thread->sinceRollup += bytes;
}

void Runtime::becomeParallel() {
  gParallel.store(true, std::memory_order_relaxed);
}

Thread* Runtime::attachThread(VM* vm) {
  Thread* thread = newThread();
  adoptThread(thread, vm);
  return thread;
}

Thread* Runtime::newThread() {
  // Registered parked, because it is not running yet. The collector can
  // then go ahead without waiting for a thread that has not started, and
  // there is nothing on it to scan.
  Thread* thread = new Thread();
  thread->parked.store(true, std::memory_order_release);
  // The caller may be trying to spawn while another thread is already
  // stopping the world. Park it before taking worldMutex_: otherwise the
  // collector waits for this caller while the caller waits for the lock.
  Thread* caller = t_thread;
  if (caller != nullptr) {
    caller->parked.store(true, std::memory_order_release);
    worldCond_.notify_all();
  }
  std::unique_lock<std::mutex> guard(worldMutex_);
  // The collector walks this list without holding the mutex, so it must
  // not grow underneath it.
  while (collecting_) worldCond_.wait(guard);
  thread->index = threads_.size();
  threads_.push_back(thread);
  if (caller != nullptr) {
    caller->parked.store(false, std::memory_order_release);
  }
  worldCond_.notify_all();
  return thread;
}

void Runtime::adoptThread(Thread* thread, VM* vm) {
  t_thread = thread;
  t_runtime = this;
  {
    std::unique_lock<std::mutex> guard(worldMutex_);
    // The collector reads this while it walks the threads, so it is set
    // between collections rather than during one.
    while (collecting_) worldCond_.wait(guard);
    thread->vm = vm;
    thread->parked.store(false, std::memory_order_release);
  }
  worldCond_.notify_all();
}

void Runtime::detachThread(Thread* thread) {
  // The thread has stopped running Red code before it can detach, so make
  // its stack safe for a collector before waiting for the world lock. A
  // collector may already be stopping the world while this thread is on
  // its way out; waiting for that lock first would leave the collector
  // waiting on this thread in return.
  thread->parked.store(true, std::memory_order_release);
  worldCond_.notify_all();
  std::unique_lock<std::mutex> guard(worldMutex_);
  // Same reason as newThread: the list this thread is about to leave is
  // one the collector may be walking.
  while (collecting_) worldCond_.wait(guard);
  if (thread->index < threads_.size() && threads_[thread->index] == thread) {
    size_t i = thread->index;
    {
    thread->vm = nullptr;
    // What this thread allocated outlives it, so the objects move to
    // the thread that is left rather than being freed or orphaned.
    if (thread->objects != nullptr && !threads_.empty()) {
      Thread* keeper = threads_[0] == thread
                           ? (threads_.size() > 1 ? threads_[1] : nullptr)
                           : threads_[0];
      if (keeper != nullptr) {
        Obj* last = thread->objects;
        while (last->next != nullptr) last = last->next;
        last->next = keeper->objects;
        keeper->objects = thread->objects;
        keeper->bytesAllocated += thread->bytesAllocated;
        thread->objects = nullptr;
        thread->bytesAllocated = 0;
      }
    }
    }
    // Swapped with the last entry rather than erased from the middle.
    // The collector walks this list as a set, so order means nothing to
    // it.
    threads_[i] = threads_.back();
    threads_[i]->index = i;
    threads_.pop_back();
  }
  // A thread that has gone must not hold the collector up.
  worldCond_.notify_all();
  if (t_thread == thread) {
    t_thread = nullptr;
    t_runtime = nullptr;
  }
}

void Runtime::clearThreadVM(Thread* thread, VM* vm) {
  if (thread == nullptr) return;
  std::unique_lock<std::mutex> guard(worldMutex_);
  while (collecting_) worldCond_.wait(guard);
  if (thread->vm == vm) thread->vm = nullptr;
}

void Runtime::reachSafepoint() {
  std::unique_lock<std::mutex> guard(worldMutex_);
  Thread* self = t_thread;
  if (self == nullptr) return;
  self->parked.store(true, std::memory_order_release);
  worldCond_.notify_all();
  while (gcPending.load(std::memory_order_relaxed)) worldCond_.wait(guard);
  self->parked.store(false, std::memory_order_release);
}

void Runtime::park() {
  if (t_thread != nullptr) {
    t_thread->parked.store(true, std::memory_order_release);
  }
  worldCond_.notify_all();
}

void Runtime::unpark() {
  std::unique_lock<std::mutex> guard(worldMutex_);
  // A collection that is already under way has to finish first: it may
  // be looking at this thread's stack right now.
  while (gcPending.load(std::memory_order_relaxed)) worldCond_.wait(guard);
  if (t_thread != nullptr) {
    t_thread->parked.store(false, std::memory_order_release);
  }
}

void Runtime::lockParked(std::mutex& mutex) {
  // The uncontended case is the usual one and costs nothing extra.
  if (mutex.try_lock()) return;
  park();
  mutex.lock();
  unpark();
}

void Runtime::stopWorld(std::unique_lock<std::mutex>& guard) {
  gcPending.store(true, std::memory_order_relaxed);
  Thread* self = t_thread;
  if (self != nullptr) self->parked.store(true, std::memory_order_release);

  for (;;) {
    bool allParked = true;
    for (Thread* thread : threads_) {
      if (!thread->parked.load(std::memory_order_acquire)) {
        allParked = false;
        break;
      }
    }
    if (allParked) break;
    worldCond_.wait(guard);
  }
}

void Runtime::startWorld() {
  gcPending.store(false, std::memory_order_relaxed);
  if (t_thread != nullptr) {
    t_thread->parked.store(false, std::memory_order_release);
  }
  worldCond_.notify_all();
}

Runtime::Runtime() {
  // The thread that builds the Runtime is the first one that can
  // allocate, so it needs an entry before anything else happens.
  mainThread_ = attachThread(nullptr);

  initString = internString("init");
  messageString = internString("message");
  joinString = internString("join");
  strString = internString("str");
  eqString = internString("eq");
  runtimeKind = internString("runtime");
}

Runtime::~Runtime() {
  // Tear the whole heap down. Order does not matter because nothing runs
  // after this point, and neither does which thread allocated what.
  for (Thread* thread : threads_) {
    Obj* obj = thread->objects;
    while (obj != nullptr) {
      Obj* next = obj->next;
      freeObject(obj);
      obj = next;
    }
    thread->objects = nullptr;
  }
  detachThread(mainThread_);
  delete mainThread_;
}

// ---- allocation -----------------------------------------------------

template <typename T>
static T* makeObject(Runtime& rt, ObjType type) {
  Thread* self = Runtime::currentThread();
  T* object = new T();
  object->obj.type = type;
  object->obj.isMarked = false;
  object->obj.lockDepth = 0;
  object->obj.unused = 0;
  object->obj.owner.store(0, std::memory_order_relaxed);
  object->obj.next = self->objects;
  self->objects = (Obj*)object;
  self->bytesAllocated += sizeof(T);
  self->sinceRollup += sizeof(T);
  (void)rt;
  return object;
}

#define NEW_OBJECT(Type, tag) \
  (maybeCollect(), makeObject<Type>(*this, ObjType::tag))

namespace {

// Strings this short are shared; longer ones are not. Two characters
// covers what a program makes over and over: the characters of the text
// it is walking, and the one and two character tokens a scanner builds.
// Beyond that a new string is usually genuinely new, and interning it
// costs a table insert, a weak-table entry and a slot in the sweep, to
// save a comparison that valuesEqual() already does on contents.
constexpr size_t kInternMaxLength = 2;

}  // namespace

ObjString* Runtime::allocateString(char* chars, size_t length, uint32_t hash,
                                   bool share) {
  ObjString* string = NEW_OBJECT(ObjString, String);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  // The characters go on the same count as the object itself, or the
  // sweep would take them off a total they were never added to.
  noteBytes(length + 1);

  if (share) {
    // The interner must not be the only thing keeping the string alive
    // while the table resizes, so root it across the insert.
    pushRoot((Obj*)string);
    strings.set(string, nilValue());
    popRoot();
  }
  return string;
}

ObjString* Runtime::copyString(const char* chars, size_t length) {
  uint32_t hash = hashString(chars, length);
  bool share = length <= kInternMaxLength;
  if (!share) {
    char* heapChars = new char[length + 1];
    std::memcpy(heapChars, chars, length);
    heapChars[length] = '\0';
    return allocateString(heapChars, length, hash, false);
  }

  // Held across the lookup and the insert both, so that two threads
  // asking for the same short string get the same object.
  InternLock locked(*this);
  ObjString* interned = strings.findString(chars, length, hash);
  if (interned != nullptr) return interned;

  char* heapChars = new char[length + 1];
  std::memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash, true);
}

ObjString* Runtime::takeString(char* chars, size_t length) {
  uint32_t hash = hashString(chars, length);
  bool share = length <= kInternMaxLength;
  if (share) {
    InternLock locked(*this);
    ObjString* interned = strings.findString(chars, length, hash);
    if (interned != nullptr) {
      // Someone already owns an identical string, so the buffer handed
      // to us is dead weight.
      delete[] chars;
      return interned;
    }
    return allocateString(chars, length, hash, true);
  }
  return allocateString(chars, length, hash, false);
}

ObjString* Runtime::copyString(const std::string& text) {
  return copyString(text.data(), text.size());
}

ObjString* Runtime::internString(const char* chars, size_t length) {
  uint32_t hash = hashString(chars, length);
  InternLock locked(*this);
  ObjString* interned = strings.findString(chars, length, hash);
  if (interned != nullptr) return interned;

  char* heapChars = new char[length + 1];
  std::memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return allocateString(heapChars, length, hash, true);
}

ObjString* Runtime::internString(const std::string& text) {
  return internString(text.data(), text.size());
}

ObjFunction* Runtime::newFunction(ObjModule* module) {
  ObjFunction* fn = NEW_OBJECT(ObjFunction, Function);
  fn->arity = 0;
  fn->maxArity = 0;
  fn->hasRest = false;
  fn->upvalueCount = 0;
  fn->slotCount = 0;
  fn->name = nullptr;
  fn->module = module;
  return fn;
}

ObjNative* Runtime::newNative(NativeFn function, const std::string& name,
                              int arity) {
  ObjString* nameString = internString(name);
  pushRoot((Obj*)nameString);
  ObjNative* native = NEW_OBJECT(ObjNative, Native);
  native->function = function;
  native->foreign = nullptr;
  native->name = nameString;
  native->arity = arity;
  popRoot();
  return native;
}

ObjClosure* Runtime::newClosure(ObjFunction* function) {
  pushRoot((Obj*)function);
  ObjClosure* closure = NEW_OBJECT(ObjClosure, Closure);
  closure->function = function;
  closure->upvalueCount = function->upvalueCount;
  closure->upvalues = new ObjUpvalue*[function->upvalueCount];
  for (int i = 0; i < function->upvalueCount; i++) closure->upvalues[i] = nullptr;
  noteBytes(sizeof(ObjUpvalue*) * (size_t)function->upvalueCount);
  popRoot();
  return closure;
}

ObjUpvalue* Runtime::newUpvalue(Value* slot) {
  ObjUpvalue* upvalue = NEW_OBJECT(ObjUpvalue, Upvalue);
  upvalue->location = slot;
  upvalue->closed = nilValue();
  upvalue->next = nullptr;
  return upvalue;
}

ObjClass* Runtime::newClass(ObjString* name) {
  pushRoot((Obj*)name);
  ObjClass* klass = NEW_OBJECT(ObjClass, Class);
  klass->name = name;
  klass->superclass = nullptr;
  popRoot();
  return klass;
}

ObjEnum* Runtime::newEnum(ObjString* name) {
  pushRoot((Obj*)name);
  ObjEnum* enumeration = NEW_OBJECT(ObjEnum, Enum);
  enumeration->name = name;
  popRoot();
  return enumeration;
}

ObjEnumMember* Runtime::newEnumMember(ObjEnum* parent, ObjString* name,
                                      double value) {
  pushRoot((Obj*)parent);
  pushRoot((Obj*)name);
  ObjEnumMember* member = NEW_OBJECT(ObjEnumMember, EnumMember);
  member->parent = parent;
  member->name = name;
  member->value = value;
  popRoot();
  popRoot();
  return member;
}

ObjInstance* Runtime::newInstance(ObjClass* klass) {
  pushRoot((Obj*)klass);
  ObjInstance* instance = NEW_OBJECT(ObjInstance, Instance);
  instance->klass = klass;
  popRoot();
  return instance;
}

ObjBoundMethod* Runtime::newBoundMethod(Value receiver, Value method) {
  GCRoot receiverRoot(*this, receiver);
  GCRoot methodRoot(*this, method);
  ObjBoundMethod* bound = NEW_OBJECT(ObjBoundMethod, BoundMethod);
  bound->receiver = receiver;
  bound->method = method;
  return bound;
}

ObjArray* Runtime::newArray() { return NEW_OBJECT(ObjArray, Array); }

ObjMap* Runtime::newMap() { return NEW_OBJECT(ObjMap, Map); }

ObjSet* Runtime::newSet() { return NEW_OBJECT(ObjSet, Set); }

ObjModule* Runtime::newModule(ObjString* name, ObjString* path) {
  pushRoot((Obj*)name);
  pushRoot((Obj*)path);
  ObjModule* module = NEW_OBJECT(ObjModule, Module);
  module->name = name;
  module->path = path;
  module->loaded = false;
  popRoot();
  popRoot();
  return module;
}

ObjChannel* Runtime::newChannel(size_t capacity) {
  ObjChannel* channel = NEW_OBJECT(ObjChannel, Channel);
  channel->capacity = capacity;
  channel->closed = false;
  channel->waiters = 0;
  return channel;
}

ObjTask* Runtime::newTask() {
  ObjTask* task = NEW_OBJECT(ObjTask, Task);
  task->thread = nullptr;
  task->vm = nullptr;
  task->result = nilValue();
  task->callee = nilValue();
  task->done = false;
  task->failed = false;
  task->joined = false;
  task->reaped.store(false, std::memory_order_relaxed);
  task->error = nilValue();
  task->waiters.clear();
  task->liveIndex = -1;
  return task;
}

ObjFile* Runtime::newFile(FILE* handle, ObjString* path) {
  pushRoot((Obj*)path);
  ObjFile* file = NEW_OBJECT(ObjFile, File);
  file->handle = handle;
  file->path = path;
  file->open = handle != nullptr;
  popRoot();
  return file;
}

ObjSocket* Runtime::newSocket(int fd, bool listening) {
  ObjSocket* socket = NEW_OBJECT(ObjSocket, Socket);
  socket->fd = fd;
  socket->listening = listening;
  socket->closed = false;
  socket->timeout = 0;
  return socket;
}

ObjRegex* Runtime::newRegex(Regex* program) {
  ObjRegex* regex = NEW_OBJECT(ObjRegex, Regex);
  regex->program = program;
  return regex;
}

ObjNativeLib* Runtime::newNativeLib(void* handle, ObjString* path) {
  pushRoot((Obj*)path);
  ObjNativeLib* lib = NEW_OBJECT(ObjNativeLib, NativeLib);
  lib->handle = handle;
  lib->path = path;
  popRoot();
  return lib;
}

ObjTypeDesc* Runtime::newTypeDesc(TypeKind kind, ObjString* name,
                                  std::vector<ObjTypeDesc*> parts) {
  pushRoot((Obj*)name);
  for (ObjTypeDesc* part : parts) pushRoot((Obj*)part);
  ObjTypeDesc* type = NEW_OBJECT(ObjTypeDesc, Type);
  type->kind = kind;
  type->name = name;
  type->parts = std::move(parts);
  type->resolved = nilValue();
  type->didResolve = false;
  for (size_t i = 0; i < type->parts.size(); i++) popRoot();
  popRoot();
  return type;
}

ObjError* Runtime::newError(ObjString* message, ObjString* trace,
                            Value payload, ObjString* kind) {
  pushRoot((Obj*)message);
  pushRoot((Obj*)trace);
  pushRoot((Obj*)(kind == nullptr ? runtimeKind : kind));
  GCRoot payloadRoot(*this, payload);
  ObjError* error = NEW_OBJECT(ObjError, Error);
  error->message = message;
  error->trace = trace;
  error->payload = payload;
  error->kind = kind == nullptr ? runtimeKind : kind;
  popRoot();
  popRoot();
  popRoot();
  return error;
}

#undef NEW_OBJECT

// ---- roots ----------------------------------------------------------

void Runtime::pushRoot(Obj* obj) { t_thread->tempRoots.push_back(obj); }
void Runtime::popRoot() { t_thread->tempRoots.pop_back(); }

void Runtime::registerTask(ObjTask* task) {
  // Tasks are spawned from whichever task happens to be running, so this
  // list is shared and needs the same guard the rest of the shared state
  // has. It used to be written without one.
  std::lock_guard<std::mutex> guard(taskListMutex_);
  task->liveIndex = (long)liveTasks_.size();
  liveTasks_.push_back(task);
}

void Runtime::retireTask(ObjTask* task) {
  std::lock_guard<std::mutex> guard(taskListMutex_);
  // Swapped with the last entry rather than searched for. This list is
  // only a root set, so its order says nothing.
  long index = task->liveIndex;
  if (index < 0 || index >= (long)liveTasks_.size() ||
      liveTasks_[(size_t)index] != task) {
    return;
  }
  liveTasks_[(size_t)index] = liveTasks_.back();
  liveTasks_[(size_t)index]->liveIndex = index;
  liveTasks_.pop_back();
  task->liveIndex = -1;
}

void Runtime::reapTask(ObjTask* task) {
  if (task->thread == nullptr) return;
  // Exactly one of the places that can join a task actually does. The
  // others find the flag already set and leave it alone.
  if (task->reaped.exchange(true, std::memory_order_acq_rel)) return;
  if (task->thread->joinable()) task->thread->join();
}

void Runtime::joinAllTasks() {
  std::vector<ObjTask*> pending;
  {
    std::lock_guard<std::mutex> guard(taskListMutex_);
    pending = liveTasks_;
  }
  // Joined outside the lock, because a task that is still running needs
  // it to make progress and reach its end. Parked as well: a task on its
  // way out may be waiting for a collection, and a collection waits for
  // every thread including this one.
  park();
  for (ObjTask* task : pending) reapTask(task);
  unpark();
}

// ---- collector ------------------------------------------------------

void Runtime::markObject(Obj* obj) {
  if (obj == nullptr || obj->isMarked) return;
  obj->isMarked = true;
  grayStack_.push_back(obj);
}

void Runtime::markValue(Value value) {
  if (isObj(value)) markObject(asObj(value));
}

void Runtime::markRoots() {
  // Every task's stack, call frames and open upvalues. Every thread is
  // parked, so none of these is moving.
  for (Thread* thread : threads_) {
    if (!thread->parked.load(std::memory_order_acquire)) {
      std::fprintf(stderr, "[gc] a thread was still running\n");
      std::abort();
    }
    if (thread->vm != nullptr) thread->vm->markRoots();
  }

  markTable(*this, modules);
  markTable(*this, builtins);
  for (Table* table : rootTables) markTable(*this, *table);

  // Every thread's temporary roots, not only the collector's: another
  // thread is parked part way through building something.
  for (Thread* thread : threads_) {
    for (Obj* obj : thread->tempRoots) markObject(obj);
  }
  {
    std::lock_guard<std::mutex> guard(taskListMutex_);
    for (ObjTask* task : liveTasks_) markObject((Obj*)task);
  }

  markObject((Obj*)initString);
  markObject((Obj*)messageString);
  markObject((Obj*)joinString);
  markObject((Obj*)strString);
  markObject((Obj*)eqString);
  markObject((Obj*)runtimeKind);
  markObject((Obj*)mainModule);
  for (ObjTypeDesc* type : simpleTypes) markObject((Obj*)type);
}

void Runtime::blackenObject(Obj* obj) {
  switch (obj->type) {
    case ObjType::String:
      break;
    case ObjType::Upvalue:
      markValue(((ObjUpvalue*)obj)->closed);
      break;
    case ObjType::Function: {
      ObjFunction* fn = (ObjFunction*)obj;
      markObject((Obj*)fn->name);
      markObject((Obj*)fn->module);
      for (Value constant : fn->chunk.constants) markValue(constant);
      break;
    }
    case ObjType::Native:
      markObject((Obj*)((ObjNative*)obj)->name);
      break;
    case ObjType::Closure: {
      ObjClosure* closure = (ObjClosure*)obj;
      markObject((Obj*)closure->function);
      for (int i = 0; i < closure->upvalueCount; i++) {
        markObject((Obj*)closure->upvalues[i]);
      }
      break;
    }
    case ObjType::Class: {
      ObjClass* klass = (ObjClass*)obj;
      markObject((Obj*)klass->name);
      markObject((Obj*)klass->superclass);
      markTable(*this, klass->methods);
      break;
    }
    case ObjType::Enum: {
      ObjEnum* enumeration = (ObjEnum*)obj;
      markObject((Obj*)enumeration->name);
      markTable(*this, enumeration->members);
      for (Value member : enumeration->ordered) markValue(member);
      break;
    }
    case ObjType::EnumMember: {
      ObjEnumMember* member = (ObjEnumMember*)obj;
      markObject((Obj*)member->parent);
      markObject((Obj*)member->name);
      break;
    }
    case ObjType::Instance: {
      ObjInstance* instance = (ObjInstance*)obj;
      markObject((Obj*)instance->klass);
      markTable(*this, instance->fields);
      break;
    }
    case ObjType::BoundMethod: {
      ObjBoundMethod* bound = (ObjBoundMethod*)obj;
      markValue(bound->receiver);
      markValue(bound->method);
      break;
    }
    case ObjType::Array:
      for (Value item : ((ObjArray*)obj)->items) markValue(item);
      break;
    case ObjType::Map: {
      ObjMap* map = (ObjMap*)obj;
      for (const ValueEntry& slot : map->entries.slots()) {
        if (!slot.used) continue;
        markValue(slot.key);
        markValue(slot.value);
      }
      break;
    }
    case ObjType::Set: {
      for (const ValueEntry& slot : ((ObjSet*)obj)->entries.slots()) {
        if (slot.used) markValue(slot.key);
      }
      break;
    }
    case ObjType::Module: {
      ObjModule* module = (ObjModule*)obj;
      markObject((Obj*)module->name);
      markObject((Obj*)module->path);
      markTable(*this, module->globals);
      break;
    }
    case ObjType::Channel: {
      // Values parked in a channel are live. This is why channel state is
      // guarded by the runtime lock rather than a private mutex: the
      // collector runs while holding that lock and reads the buffer here.
      for (Value value : ((ObjChannel*)obj)->buffer) markValue(value);
      break;
    }
    case ObjType::Task: {
      ObjTask* task = (ObjTask*)obj;
      markValue(task->result);
      markValue(task->callee);
      for (Value arg : task->args) markValue(arg);
      markValue(task->error);
      break;
    }
    case ObjType::File:
      markObject((Obj*)((ObjFile*)obj)->path);
      break;
    case ObjType::Socket:
      break;
    case ObjType::Regex:
      break;
    case ObjType::NativeLib:
      markObject((Obj*)((ObjNativeLib*)obj)->path);
      break;
    case ObjType::Error: {
      ObjError* error = (ObjError*)obj;
      markObject((Obj*)error->message);
      markObject((Obj*)error->trace);
      markObject((Obj*)error->kind);
      markValue(error->payload);
      break;
    }
    case ObjType::Type: {
      ObjTypeDesc* type = (ObjTypeDesc*)obj;
      markObject((Obj*)type->name);
      for (ObjTypeDesc* part : type->parts) markObject((Obj*)part);
      markValue(type->resolved);
      break;
    }
  }
}

void Runtime::traceReferences() {
  while (!grayStack_.empty()) {
    Obj* obj = grayStack_.back();
    grayStack_.pop_back();
    blackenObject(obj);
  }
}

// Returns how many bytes it gave back, so that sweep can work out what
// a thread still holds without keeping a second count in step.
size_t Runtime::freeObject(Obj* obj) {
  size_t freed = 0;
  switch (obj->type) {
    case ObjType::String: {
      ObjString* string = (ObjString*)obj;
      freed += string->length + 1;
      delete[] string->chars;
      freed += sizeof(ObjString);
      delete string;
      break;
    }
    case ObjType::Function:
      freed += sizeof(ObjFunction);
      delete (ObjFunction*)obj;
      break;
    case ObjType::Native:
      freed += sizeof(ObjNative);
      delete (ObjNative*)obj;
      break;
    case ObjType::Closure: {
      ObjClosure* closure = (ObjClosure*)obj;
      freed += sizeof(ObjUpvalue*) * (size_t)closure->upvalueCount;
      delete[] closure->upvalues;
      freed += sizeof(ObjClosure);
      delete closure;
      break;
    }
    case ObjType::Upvalue:
      freed += sizeof(ObjUpvalue);
      delete (ObjUpvalue*)obj;
      break;
    case ObjType::Class:
      freed += sizeof(ObjClass);
      delete (ObjClass*)obj;
      break;
    case ObjType::Instance:
      freed += sizeof(ObjInstance);
      delete (ObjInstance*)obj;
      break;
    case ObjType::BoundMethod:
      freed += sizeof(ObjBoundMethod);
      delete (ObjBoundMethod*)obj;
      break;
    case ObjType::Array:
      freed += sizeof(ObjArray);
      delete (ObjArray*)obj;
      break;
    case ObjType::Map:
      freed += sizeof(ObjMap);
      delete (ObjMap*)obj;
      break;
    case ObjType::Set:
      freed += sizeof(ObjSet);
      delete (ObjSet*)obj;
      break;
    case ObjType::Module:
      freed += sizeof(ObjModule);
      delete (ObjModule*)obj;
      break;
    case ObjType::Enum:
      freed += sizeof(ObjEnum);
      delete (ObjEnum*)obj;
      break;
    case ObjType::EnumMember:
      freed += sizeof(ObjEnumMember);
      delete (ObjEnumMember*)obj;
      break;
    case ObjType::Channel:
      freed += sizeof(ObjChannel);
      delete (ObjChannel*)obj;
      break;
    case ObjType::Task: {
      ObjTask* task = (ObjTask*)obj;
      // A task is only collectable once it has finished, so the thread is
      // always joinable here rather than still running.
      if (task->thread != nullptr) {
        // Only collectable once it has finished and been joined, so
        // this is nearly always already done.
        reapTask(task);
        delete task->thread;
      }
      freed += sizeof(ObjTask);
      delete task;
      break;
    }
    case ObjType::File: {
      ObjFile* file = (ObjFile*)obj;
      // Closing on collection is a convenience, not a guarantee. Programs
      // that care about flush order should call close().
      if (file->open && file->handle != nullptr) std::fclose(file->handle);
      freed += sizeof(ObjFile);
      delete file;
      break;
    }
    case ObjType::Socket: {
      ObjSocket* socket = (ObjSocket*)obj;
      if (!socket->closed && socket->fd >= 0) ::close(socket->fd);
      freed += sizeof(ObjSocket);
      delete socket;
      break;
    }
    case ObjType::Regex: {
      ObjRegex* regex = (ObjRegex*)obj;
      delete regex->program;
      freed += sizeof(ObjRegex);
      delete regex;
      break;
    }
    case ObjType::NativeLib:
      freed += sizeof(ObjNativeLib);
      delete (ObjNativeLib*)obj;
      break;
    case ObjType::Error:
      freed += sizeof(ObjError);
      delete (ObjError*)obj;
      break;
    case ObjType::Type:
      freed += sizeof(ObjTypeDesc);
      delete (ObjTypeDesc*)obj;
      break;
  }
  return freed;
}

void Runtime::sweep() {
  // Each thread's list is swept on its own, and its share of the total
  // is worked out again from what survived. Counting up rather than
  // subtracting as objects are freed means the totals cannot drift.
  size_t live = 0;
  for (Thread* thread : threads_) {
    Obj* previous = nullptr;
    Obj* object = thread->objects;
    size_t freed = 0;
    while (object != nullptr) {
      if (object->isMarked) {
        object->isMarked = false;
        previous = object;
        object = object->next;
      } else {
        Obj* unreached = object;
        object = object->next;
        if (previous != nullptr) {
          previous->next = object;
        } else {
          thread->objects = object;
        }
        freed += freeObject(unreached);
      }
    }
    size_t kept = thread->bytesAllocated - freed;
    thread->bytesAllocated = kept;
    thread->sinceRollup = 0;
    live += kept;
  }
  bytesAllocated.store(live, std::memory_order_relaxed);
}

void Runtime::collectGarbage() {
  // Claim collection before stopping the world. Otherwise two threads can
  // both pass the old check while the first collector is still parking the
  // others, and then mutate grayStack_ at the same time.
  std::unique_lock<std::mutex> guard(worldMutex_);
  if (collecting_) return;
  collecting_ = true;
  stopWorld(guard);
  // The world is stopped by gcPending rather than by this mutex, so it
  // can be let go: a thread that wants to park while the collector
  // works has to be able to, or a channel operation could hold a lock
  // the collector is waiting on.
  guard.unlock();

  size_t before = bytesAllocated.load(std::memory_order_relaxed);
  if (logGC) std::fprintf(stderr, "[gc] begin, %zu bytes\n", before);

  markRoots();
  traceReferences();
  // The interner holds every live string but must not keep any alive. Drop
  // its entries for strings nothing else reached, then sweep.
  strings.removeUnmarked();
  sweep();
  // Every thread is parked, so none of them is inside a table lookup.
  // That makes this the one safe moment to let the arrays that resizes
  // replaced go.
  Table::releaseRetiredArrays();

  size_t live = bytesAllocated.load(std::memory_order_relaxed);
  nextGC = live * kHeapGrowFactor;
  if (nextGC < 1024 * 1024) nextGC = 1024 * 1024;
  collections++;
  if (live > peakBytes) peakBytes = live;

  if (logGC) {
    std::fprintf(stderr, "[gc] end, %zu bytes freed, %zu live, next at %zu\n",
                 before - live, live, nextGC);
  }
  guard.lock();
  collecting_ = false;
  startWorld();
}

void Runtime::maybeCollect() {
  Thread* self = t_thread;

  // What this thread has allocated since the last time is folded into
  // the shared total now and then rather than on every object, so an
  // allocation costs a plain add and one relaxed load.
  if (self != nullptr && self->sinceRollup >= kRollupBytes) {
    bytesAllocated.fetch_add(self->sinceRollup, std::memory_order_relaxed);
    self->sinceRollup = 0;
  }

  // Another thread may be waiting to collect. Allocating is a place
  // where this thread's stack is in a state the collector understands,
  // so it is a safepoint.
  safepoint();

  // Stress mode collects before every allocation. It makes the test suite
  // slow and makes missing roots fail immediately instead of rarely.
  if (stressGC) {
    if (self != nullptr) {
      bytesAllocated.fetch_add(self->sinceRollup, std::memory_order_relaxed);
      self->sinceRollup = 0;
    }
    collectGarbage();
    return;
  }
  if (bytesAllocated.load(std::memory_order_relaxed) > nextGC) {
    if (self != nullptr) {
      bytesAllocated.fetch_add(self->sinceRollup, std::memory_order_relaxed);
      self->sinceRollup = 0;
    }
    collectGarbage();
  }
}

}  // namespace red
