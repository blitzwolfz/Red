#include "runtime.h"

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

void markTable(Runtime& rt, Table& table) {
  for (int i = 0; i < table.capacity(); i++) {
    Entry* entry = &table.entries()[i];
    if (entry->key != nullptr) rt.markObject((Obj*)entry->key);
    rt.markValue(entry->value);
  }
}

}  // namespace

Runtime::Runtime() {
  initString = internString("init");
  messageString = internString("message");
}

Runtime::~Runtime() {
  // Tear the whole heap down. Order does not matter because nothing runs
  // after this point.
  Obj* obj = objects_;
  while (obj != nullptr) {
    Obj* next = obj->next;
    freeObject(obj);
    obj = next;
  }
}

// ---- allocation -----------------------------------------------------

template <typename T>
static T* makeObject(Runtime& rt, Obj** listHead, ObjType type,
                     size_t* bytesAllocated) {
  T* object = new T();
  object->obj.type = type;
  object->obj.isMarked = false;
  object->obj.next = *listHead;
  *listHead = (Obj*)object;
  *bytesAllocated += sizeof(T);
  (void)rt;
  return object;
}

#define NEW_OBJECT(Type, tag)                                     \
  (maybeCollect(),                                                \
   makeObject<Type>(*this, &objects_, ObjType::tag, &bytesAllocated))

ObjString* Runtime::copyString(const char* chars, size_t length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = strings.findString(chars, length, hash);
  if (interned != nullptr) return interned;

  char* heapChars = new char[length + 1];
  std::memcpy(heapChars, chars, length);
  heapChars[length] = '\0';
  return takeString(heapChars, length);
}

ObjString* Runtime::takeString(char* chars, size_t length) {
  uint32_t hash = hashString(chars, length);
  ObjString* interned = strings.findString(chars, length, hash);
  if (interned != nullptr) {
    // Someone already owns an identical string, so the buffer handed to us
    // is dead weight.
    delete[] chars;
    return interned;
  }

  ObjString* string = NEW_OBJECT(ObjString, String);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  bytesAllocated += length + 1;

  // The interner must not be the only thing keeping the string alive while
  // the table resizes, so root it across the insert.
  pushRoot((Obj*)string);
  strings.set(string, nilValue());
  popRoot();
  return string;
}

ObjString* Runtime::internString(const std::string& text) {
  return copyString(text.data(), text.size());
}

ObjFunction* Runtime::newFunction(ObjModule* module) {
  ObjFunction* fn = NEW_OBJECT(ObjFunction, Function);
  fn->arity = 0;
  fn->upvalueCount = 0;
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
  bytesAllocated += sizeof(ObjUpvalue*) * (size_t)function->upvalueCount;
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
  popRoot();
  return klass;
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
  task->errorMessage = nullptr;
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
  return socket;
}

ObjNativeLib* Runtime::newNativeLib(void* handle, ObjString* path) {
  pushRoot((Obj*)path);
  ObjNativeLib* lib = NEW_OBJECT(ObjNativeLib, NativeLib);
  lib->handle = handle;
  lib->path = path;
  popRoot();
  return lib;
}

ObjError* Runtime::newError(ObjString* message, ObjString* trace,
                            Value payload) {
  pushRoot((Obj*)message);
  pushRoot((Obj*)trace);
  GCRoot payloadRoot(*this, payload);
  ObjError* error = NEW_OBJECT(ObjError, Error);
  error->message = message;
  error->trace = trace;
  error->payload = payload;
  popRoot();
  popRoot();
  return error;
}

#undef NEW_OBJECT

// ---- roots ----------------------------------------------------------

void Runtime::pushRoot(Obj* obj) { tempRoots_.push_back(obj); }
void Runtime::popRoot() { tempRoots_.pop_back(); }

void Runtime::registerVM(VM* vm) { vms_.push_back(vm); }

void Runtime::unregisterVM(VM* vm) {
  for (size_t i = 0; i < vms_.size(); i++) {
    if (vms_[i] == vm) {
      vms_.erase(vms_.begin() + (long)i);
      return;
    }
  }
}

void Runtime::registerTask(ObjTask* task) { liveTasks_.push_back(task); }

void Runtime::retireTask(ObjTask* task) {
  for (size_t i = 0; i < liveTasks_.size(); i++) {
    if (liveTasks_[i] == task) {
      liveTasks_.erase(liveTasks_.begin() + (long)i);
      return;
    }
  }
}

void Runtime::joinAllTasks() {
  std::vector<ObjTask*> pending;
  {
    std::lock_guard<std::mutex> guard(lock);
    pending = liveTasks_;
  }
  // Joined outside the lock, because a task that is still running needs
  // the lock to make progress and reach its end.
  for (ObjTask* task : pending) {
    if (task->thread != nullptr && task->thread->joinable()) {
      task->thread->join();
    }
  }
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
  // Every task's stack, call frames and open upvalues. Tasks that are not
  // running are parked with a stable stack, so this is safe.
  for (VM* vm : vms_) vm->markRoots();

  markTable(*this, modules);
  markTable(*this, builtins);
  for (Table* table : rootTables) markTable(*this, *table);

  for (Obj* obj : tempRoots_) markObject(obj);
  for (ObjTask* task : liveTasks_) markObject((Obj*)task);

  markObject((Obj*)initString);
  markObject((Obj*)messageString);
  markObject((Obj*)mainModule);
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
      markTable(*this, klass->methods);
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
      markObject((Obj*)task->errorMessage);
      break;
    }
    case ObjType::File:
      markObject((Obj*)((ObjFile*)obj)->path);
      break;
    case ObjType::Socket:
      break;
    case ObjType::NativeLib:
      markObject((Obj*)((ObjNativeLib*)obj)->path);
      break;
    case ObjType::Error: {
      ObjError* error = (ObjError*)obj;
      markObject((Obj*)error->message);
      markObject((Obj*)error->trace);
      markValue(error->payload);
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

void Runtime::freeObject(Obj* obj) {
  switch (obj->type) {
    case ObjType::String: {
      ObjString* string = (ObjString*)obj;
      bytesAllocated -= string->length + 1;
      delete[] string->chars;
      bytesAllocated -= sizeof(ObjString);
      delete string;
      break;
    }
    case ObjType::Function:
      bytesAllocated -= sizeof(ObjFunction);
      delete (ObjFunction*)obj;
      break;
    case ObjType::Native:
      bytesAllocated -= sizeof(ObjNative);
      delete (ObjNative*)obj;
      break;
    case ObjType::Closure: {
      ObjClosure* closure = (ObjClosure*)obj;
      bytesAllocated -= sizeof(ObjUpvalue*) * (size_t)closure->upvalueCount;
      delete[] closure->upvalues;
      bytesAllocated -= sizeof(ObjClosure);
      delete closure;
      break;
    }
    case ObjType::Upvalue:
      bytesAllocated -= sizeof(ObjUpvalue);
      delete (ObjUpvalue*)obj;
      break;
    case ObjType::Class:
      bytesAllocated -= sizeof(ObjClass);
      delete (ObjClass*)obj;
      break;
    case ObjType::Instance:
      bytesAllocated -= sizeof(ObjInstance);
      delete (ObjInstance*)obj;
      break;
    case ObjType::BoundMethod:
      bytesAllocated -= sizeof(ObjBoundMethod);
      delete (ObjBoundMethod*)obj;
      break;
    case ObjType::Array:
      bytesAllocated -= sizeof(ObjArray);
      delete (ObjArray*)obj;
      break;
    case ObjType::Map:
      bytesAllocated -= sizeof(ObjMap);
      delete (ObjMap*)obj;
      break;
    case ObjType::Module:
      bytesAllocated -= sizeof(ObjModule);
      delete (ObjModule*)obj;
      break;
    case ObjType::Channel:
      bytesAllocated -= sizeof(ObjChannel);
      delete (ObjChannel*)obj;
      break;
    case ObjType::Task: {
      ObjTask* task = (ObjTask*)obj;
      // A task is only collectable once it has finished, so the thread is
      // always joinable here rather than still running.
      if (task->thread != nullptr) {
        if (task->thread->joinable()) task->thread->join();
        delete task->thread;
      }
      bytesAllocated -= sizeof(ObjTask);
      delete task;
      break;
    }
    case ObjType::File: {
      ObjFile* file = (ObjFile*)obj;
      // Closing on collection is a convenience, not a guarantee. Programs
      // that care about flush order should call close().
      if (file->open && file->handle != nullptr) std::fclose(file->handle);
      bytesAllocated -= sizeof(ObjFile);
      delete file;
      break;
    }
    case ObjType::Socket: {
      ObjSocket* socket = (ObjSocket*)obj;
      if (!socket->closed && socket->fd >= 0) ::close(socket->fd);
      bytesAllocated -= sizeof(ObjSocket);
      delete socket;
      break;
    }
    case ObjType::NativeLib:
      bytesAllocated -= sizeof(ObjNativeLib);
      delete (ObjNativeLib*)obj;
      break;
    case ObjType::Error:
      bytesAllocated -= sizeof(ObjError);
      delete (ObjError*)obj;
      break;
  }
}

void Runtime::sweep() {
  Obj* previous = nullptr;
  Obj* object = objects_;
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
        objects_ = object;
      }
      freeObject(unreached);
    }
  }
}

void Runtime::collectGarbage() {
  if (collecting_) return;
  collecting_ = true;

  size_t before = bytesAllocated;
  if (logGC) std::fprintf(stderr, "[gc] begin, %zu bytes\n", before);

  markRoots();
  traceReferences();
  // The interner holds every live string but must not keep any alive. Drop
  // its entries for strings nothing else reached, then sweep.
  strings.removeUnmarked();
  sweep();

  nextGC = bytesAllocated * kHeapGrowFactor;
  if (nextGC < 1024 * 1024) nextGC = 1024 * 1024;
  collections++;
  if (bytesAllocated > peakBytes) peakBytes = bytesAllocated;

  if (logGC) {
    std::fprintf(stderr, "[gc] end, %zu bytes freed, %zu live, next at %zu\n",
                 before - bytesAllocated, bytesAllocated, nextGC);
  }
  collecting_ = false;
}

void Runtime::maybeCollect() {
  if (collecting_) return;
  // Stress mode collects before every allocation. It makes the test suite
  // slow and makes missing roots fail immediately instead of rarely.
  if (stressGC || bytesAllocated > nextGC) collectGarbage();
}

}  // namespace red
