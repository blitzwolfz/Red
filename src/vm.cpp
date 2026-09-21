#include "vm.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "compiler.h"
#include "debug.h"
#include "debugger.h"
#include "serialize.h"
#include "stdlib/builtins.h"
#include "util.h"

namespace red {

namespace {

// "1 argument" rather than "1 arguments".
const char* plural(int count) { return count == 1 ? "" : "s"; }

// Entry point for a spawned task. Runs on its own thread with its own VM
// and its own value stack, sharing the heap with everyone else.
void taskMain(Runtime* runtime, ObjTask* task) {
  VM vm(*runtime);
  vm.attach();
  task->vm = &vm;

  // Rebuild the call on this task's own stack: callee first, then its
  // arguments, exactly as a normal call site would leave them.
  vm.push(task->callee);
  for (Value arg : task->args) vm.push(arg);

  Value result = nilValue();
  InterpretResult status =
      vm.callAndRun(task->callee, (int)task->args.size(), &result);

  task->result = result;
  task->failed = status != InterpretResult::Ok;
  if (task->failed) {
    // Kept as the error itself, so that join() can raise it again with
    // its kind and payload intact. The two tasks share one heap, so the
    // value is as good here as it was there.
    task->error = vm.lastError;
  }
  task->vm = nullptr;
  task->done = true;
  runtime->cond.notify_all();
  vm.detach();
}

}  // namespace

VM::VM(Runtime& runtime)
    : runtime_(runtime), lock_(runtime.lock, std::defer_lock) {
  // One contiguous block per task. Slot pointers are handed out to call
  // frames and to open upvalues, so this must never be reallocated.
  stack_ = new Value[kMaxStack];
  stackTop_ = stack_;
}

VM::~VM() { delete[] stack_; }

void VM::attach() {
  lock_.lock();
  runtime_.registerVM(this);
}

void VM::detach() {
  runtime_.unregisterVM(this);
  lock_.unlock();
}

void VM::acquireLock() { lock_.lock(); }

void VM::releaseLock() { lock_.unlock(); }

void VM::push(Value value) { *stackTop_++ = value; }

Value VM::pop() { return *--stackTop_; }

Value VM::peek(int distance) const { return stackTop_[-1 - distance]; }

ObjModule* VM::currentModule() {
  if (frameCount_ == 0) return runtime_.mainModule;
  return frames_[frameCount_ - 1].closure->function->module;
}

void VM::markRoots() {
  for (Value* slot = stack_; slot < stackTop_; slot++) {
    runtime_.markValue(*slot);
  }
  for (int i = 0; i < frameCount_; i++) {
    runtime_.markObject((Obj*)frames_[i].closure);
  }
  for (ObjUpvalue* upvalue = openUpvalues_; upvalue != nullptr;
       upvalue = upvalue->next) {
    runtime_.markObject((Obj*)upvalue);
  }
  runtime_.markValue(lastError);
  runtime_.markValue(failValue_);
}

std::string VM::buildTrace() {
  std::string trace;
  for (int i = frameCount_ - 1; i >= 0; i--) {
    CallFrame* frame = &frames_[i];
    ObjFunction* function = frame->closure->function;
    size_t offset = (size_t)(frame->ip - function->chunk.code.data() - 1);
    trace += "  at ";
    trace += function->name == nullptr
                 ? "<script>"
                 : std::string(function->name->chars, function->name->length);
    trace += " (";
    trace += std::string(function->module->path->chars,
                         function->module->path->length);
    trace += ":" + std::to_string(function->chunk.lineAt(offset)) + ")\n";
  }
  return trace;
}

Value VM::makeError(const char* kind, const std::string& text, Value payload) {
  ObjString* message = runtime_.internString(text);
  GCRoot messageRoot(runtime_, (Obj*)message);
  ObjString* kindString =
      kind == nullptr ? runtime_.runtimeKind : runtime_.internString(kind);
  GCRoot kindRoot(runtime_, (Obj*)kindString);
  GCRoot payloadRoot(runtime_, payload);
  ObjString* trace = runtime_.internString(buildTrace());
  return objValue(
      (Obj*)runtime_.newError(message, trace, payload, kindString));
}

Value VM::fail(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  failValue_ = makeError(nullptr, buffer, nilValue());
  failed_ = true;
  return nilValue();
}

Value VM::failAs(const char* kind, const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  failValue_ = makeError(kind, buffer, nilValue());
  failed_ = true;
  return nilValue();
}

Value VM::reraise(Value error) {
  failValue_ = error;
  failed_ = true;
  return nilValue();
}

bool VM::runtimeError(const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return raise(makeError(nullptr, buffer, nilValue()));
}

bool VM::runtimeErrorAs(const char* kind, const char* format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  return raise(makeError(kind, buffer, nilValue()));
}

bool VM::raise(Value value) {
  while (!handlers_.empty()) {
    // Handlers belonging to an outer run loop are left alone. The outer
    // loop gets the error back as a failed result and re-raises it there.
    if (handlers_.back().frameIndex < baseFrame_) break;

    TryHandler handler = handlers_.back();
    handlers_.pop_back();

    while (frameCount_ - 1 > handler.frameIndex) {
      closeUpvalues(frames_[frameCount_ - 1].slots);
      frameCount_--;
    }
    closeUpvalues(stack_ + handler.stackOffset);
    stackTop_ = stack_ + handler.stackOffset;
    push(value);
    frames_[frameCount_ - 1].ip = handler.catchIp;
    return true;
  }
  lastError = value;
  return false;
}

// ---------------------------------------------------------------------
// calling

bool VM::call(ObjClosure* closure, int argCount) {
  ObjFunction* function = closure->function;
  // Remembered before any padding, so the prologue can tell which
  // parameters the call actually supplied.
  int supplied = argCount;

  if (argCount < function->arity ||
      (!function->hasRest && argCount > function->maxArity)) {
    if (function->hasRest) {
      return runtimeErrorAs("arity",
                            "Expected at least %d argument%s but got %d.",
                            function->arity, plural(function->arity),
                            argCount);
    }
    if (function->arity == function->maxArity) {
      return runtimeErrorAs("arity", "Expected %d argument%s but got %d.",
                            function->arity, plural(function->arity),
                            argCount);
    }
    return runtimeErrorAs("arity",
                          "Expected between %d and %d arguments but got %d.",
                          function->arity, function->maxArity, argCount);
  }
  if (frameCount_ == kMaxFrames) {
    return runtimeErrorAs("overflow",
                          "Stack overflow: call depth exceeded %d frames.",
                          kMaxFrames);
  }
  // A frame can be far wider than the average, so depth on its own is not
  // a safe bound. Check the room this callee can actually need.
  if (stackTop_ + closure->function->slotCount > stack_ + kMaxStack) {
    return runtimeErrorAs(
        "overflow", "Stack overflow: %s needs %d slots and only %ld are left.",
        closure->function->name == nullptr
            ? "<script>"
            : closure->function->name->chars,
        closure->function->slotCount,
        (long)(stack_ + kMaxStack - stackTop_));
  }
  // Parameters the call left out get a nil, which the prologue replaces
  // with the declared default.
  while (argCount < function->maxArity) {
    push(nilValue());
    argCount++;
  }

  if (function->hasRest) {
    // Everything past the declared parameters moves into one array, which
    // then occupies the rest parameter's slot.
    int extra = argCount - function->maxArity;
    ObjArray* rest = runtime_.newArray();
    GCRoot restRoot(runtime_, (Obj*)rest);
    rest->items.assign(stackTop_ - extra, stackTop_);
    stackTop_ -= extra;
    push(objValue((Obj*)rest));
    argCount = function->maxArity + 1;
  }

  CallFrame* frame = &frames_[frameCount_++];
  frame->closure = closure;
  frame->ip = function->chunk.code.data();
  frame->slots = stackTop_ - argCount - 1;
  frame->argCount = supplied;
  return true;
}

bool VM::callValue(Value callee, int argCount) {
  if (isObj(callee)) {
    switch (asObj(callee)->type) {
      case ObjType::BoundMethod: {
        ObjBoundMethod* bound = asBoundMethod(callee);
        // Overwrite the callee slot with the receiver so the method sees
        // it as local slot zero.
        stackTop_[-argCount - 1] = bound->receiver;
        return callValue(bound->method, argCount);
      }
      case ObjType::Class: {
        ObjClass* klass = asClass(callee);
        ObjInstance* instance = runtime_.newInstance(klass);
        stackTop_[-argCount - 1] = objValue((Obj*)instance);
        Value initializer;
        if (klass->methods.get(runtime_.initString, &initializer)) {
          return call(asClosure(initializer), argCount);
        }
        if (argCount != 0) {
          return runtimeError("Expected 0 arguments but got %d.", argCount);
        }
        return true;
      }
      case ObjType::Closure:
        return call(asClosure(callee), argCount);
      case ObjType::Native: {
        ObjNative* native = asNative(callee);
        if (native->arity >= 0 && native->arity != argCount) {
          return runtimeErrorAs("arity", "%s() expected %d argument%s but got %d.",
                                native->name->chars, native->arity,
                                plural(native->arity), argCount);
        }
        Value result =
            native->foreign != nullptr
                ? native->foreign(this, argCount, stackTop_ - argCount)
                : native->function(*this, argCount, stackTop_ - argCount);
        if (failed_) {
          failed_ = false;
          Value error = failValue_;
          failValue_ = nilValue();
          stackTop_ -= argCount + 1;
          return raise(error);
        }
        stackTop_ -= argCount + 1;
        push(result);
        return true;
      }
      default:
        break;
    }
  }
  return runtimeErrorAs("type", "Can only call functions and classes, got %s.",
                        valueTypeName(callee));
}

// ---------------------------------------------------------------------
// the hooks a class may define

Value VM::classHook(Value value, ObjString* name) {
  if (!isInstance(value)) return nilValue();
  Value method;
  if (!asInstance(value)->klass->methods.get(name, &method)) return nilValue();
  // Methods are always closures, but a hook is called from places that
  // cannot report a failure, so the check is worth its one comparison.
  if (!isClosure(method)) return nilValue();
  return method;
}

std::string VM::stringifyAt(Value value, bool quoteStrings, int depth) {
  // The same bound the plain printer uses. A container can hold itself,
  // and a str() that prints its fields can reach one.
  if (depth >= 8) return quoteStrings ? valueToDisplay(value) : valueToString(value);

  Value hook = classHook(value, runtime_.strString);
  if (!isNil(hook)) {
    // The receiver goes in the callee slot, which is where a compiled
    // call site leaves it and where the method reads `this` from.
    push(value);
    Value text = nilValue();
    if (callAndRun(hook, 0, &text) != InterpretResult::Ok) {
      // The error is already raised. Fall back rather than losing the
      // line that was being printed.
      return valueToString(value);
    }
    // Whatever str() returned is shown as itself. A str() that gives
    // back something other than a string is a mistake, but printing it
    // is more useful than refusing to.
    if (isString(text)) {
      // Never quoted, even inside a container: what str() gave back is
      // the object's own written form, not a string it was holding.
      return std::string(asString(text)->chars, asString(text)->length);
    }
    return stringifyAt(text, quoteStrings, depth + 1);
  }

  // Containers are walked here rather than in value.cpp so that an
  // instance with a str() inside one goes through it.
  if (isArray(value)) {
    ObjArray* array = asArray(value);
    std::string out = "[";
    for (size_t i = 0; i < array->items.size(); i++) {
      if (i > 0) out += ", ";
      out += stringifyAt(array->items[i], true, depth + 1);
    }
    return out + "]";
  }
  if (isMap(value)) {
    ObjMap* map = asMap(value);
    std::string out = "{";
    bool first = true;
    for (const ValueEntry& slot : map->entries.slots()) {
      if (!slot.used || slot.tombstone) continue;
      if (!first) out += ", ";
      first = false;
      out += stringifyAt(slot.key, true, depth + 1);
      out += ": ";
      out += stringifyAt(slot.value, true, depth + 1);
    }
    return out + "}";
  }
  if (isSet(value)) {
    ObjSet* set = asSet(value);
    std::string out = "set(";
    bool first = true;
    for (const ValueEntry& slot : set->entries.slots()) {
      if (!slot.used || slot.tombstone) continue;
      if (!first) out += ", ";
      first = false;
      out += stringifyAt(slot.key, true, depth + 1);
    }
    return out + ")";
  }

  return quoteStrings ? valueToDisplay(value) : valueToString(value);
}

std::string VM::stringify(Value value) { return stringifyAt(value, false, 0); }

std::string VM::display(Value value) { return stringifyAt(value, true, 0); }

bool VM::equal(Value a, Value b) {
  if (valuesEqual(a, b)) return true;

  // eq() is asked only when the plain comparison already said no, so a
  // value is always equal to itself whatever the method does.
  Value hook = classHook(a, runtime_.eqString);
  if (isNil(hook)) return false;

  push(a);
  push(b);
  Value answer = nilValue();
  if (callAndRun(hook, 1, &answer) != InterpretResult::Ok) return false;
  return !isFalsey(answer);
}

bool VM::bindMethod(ObjClass* klass, ObjString* name) {
  Value method;
  if (!klass->methods.get(name, &method)) {
    return runtimeErrorAs("name", "Undefined property '%s' on %s.",
                          name->chars, klass->name->chars);
  }
  ObjBoundMethod* bound = runtime_.newBoundMethod(peek(0), method);
  pop();
  push(objValue((Obj*)bound));
  return true;
}

bool VM::getBuiltinProperty(Value receiver, ObjString* name) {
  ObjNative* method = lookupBuiltinMethod(runtime_, receiver, name);
  if (method == nullptr) return false;
  ObjBoundMethod* bound =
      runtime_.newBoundMethod(receiver, objValue((Obj*)method));
  pop();
  push(objValue((Obj*)bound));
  return true;
}

bool VM::invokeFromClass(ObjClass* klass, ObjString* name, int argCount) {
  Value method;
  if (!klass->methods.get(name, &method)) {
    return runtimeErrorAs("name", "Undefined method '%s' on %s.",
                          name->chars, klass->name->chars);
  }
  return call(asClosure(method), argCount);
}

bool VM::invoke(ObjString* name, int argCount) {
  Value receiver = peek(argCount);

  if (isInstance(receiver)) {
    ObjInstance* instance = asInstance(receiver);
    // A field holding a function shadows a method of the same name.
    Value field;
    if (instance->fields.get(name, &field)) {
      stackTop_[-argCount - 1] = field;
      return callValue(field, argCount);
    }
    return invokeFromClass(instance->klass, name, argCount);
  }

  if (isObjType(receiver, ObjType::Module)) {
    Value function;
    if (!asModule(receiver)->globals.get(name, &function)) {
      return runtimeErrorAs("name", "Undefined name '%s' in module.",
                            name->chars);
    }
    stackTop_[-argCount - 1] = function;
    return callValue(function, argCount);
  }

  if (isEnum(receiver)) {
    Value member;
    if (asEnum(receiver)->members.get(name, &member)) {
      stackTop_[-argCount - 1] = member;
      return callValue(member, argCount);
    }
  }

  // Runtime provided methods take the receiver as their first argument,
  // which is already sitting below the arguments on the stack.
  ObjNative* method = lookupBuiltinMethod(runtime_, receiver, name);
  if (method != nullptr) {
    if (method->arity >= 0 && method->arity != argCount + 1) {
      return runtimeErrorAs("arity", "%s() expected %d argument%s but got %d.",
                            method->name->chars, method->arity - 1,
                            plural(method->arity - 1), argCount);
    }
    Value result =
        method->foreign != nullptr
            ? method->foreign(this, argCount + 1, stackTop_ - argCount - 1)
            : method->function(*this, argCount + 1, stackTop_ - argCount - 1);
    if (failed_) {
      failed_ = false;
      Value error = failValue_;
      failValue_ = nilValue();
      stackTop_ -= argCount + 1;
      return raise(error);
    }
    stackTop_ -= argCount + 1;
    push(result);
    return true;
  }

  if (isClass(receiver)) {
    return runtimeError("Undefined static method '%s'.", name->chars);
  }
  return runtimeErrorAs("name", "Type %s has no method '%s'.",
                        valueTypeName(receiver), name->chars);
}

ObjUpvalue* VM::captureUpvalue(Value* local) {
  // Open upvalues are kept sorted by stack slot, highest first, so a
  // lookup stops as soon as it passes the slot it wants.
  ObjUpvalue* previous = nullptr;
  ObjUpvalue* upvalue = openUpvalues_;
  while (upvalue != nullptr && upvalue->location > local) {
    previous = upvalue;
    upvalue = upvalue->next;
  }
  if (upvalue != nullptr && upvalue->location == local) return upvalue;

  ObjUpvalue* created = runtime_.newUpvalue(local);
  created->next = upvalue;
  if (previous == nullptr) {
    openUpvalues_ = created;
  } else {
    previous->next = created;
  }
  return created;
}

void VM::closeUpvalues(Value* last) {
  while (openUpvalues_ != nullptr && openUpvalues_->location >= last) {
    ObjUpvalue* upvalue = openUpvalues_;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    openUpvalues_ = upvalue->next;
  }
}

void VM::defineMethod(ObjString* name) {
  Value method = peek(0);
  ObjClass* klass = asClass(peek(1));
  klass->methods.set(name, method);
  pop();
}

// ---------------------------------------------------------------------
// operations with enough body to deserve their own function

bool VM::concatenate() {
  ObjString* b = asString(peek(0));
  ObjString* a = asString(peek(1));
  size_t length = a->length + b->length;
  char* chars = new char[length + 1];
  std::memcpy(chars, a->chars, a->length);
  std::memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';
  ObjString* result = runtime_.takeString(chars, length);
  pop();
  pop();
  push(objValue((Obj*)result));
  return true;
}

bool VM::getIndex() {
  Value indexValue = peek(0);
  Value target = peek(1);

  if (isArray(target)) {
    if (!isNumber(indexValue)) {
      return runtimeErrorAs("type", "Array index must be a number, got %s.",
                            valueTypeName(indexValue));
    }
    ObjArray* array = asArray(target);
    double raw = asNumber(indexValue);
    long index = (long)raw;
    // Negative indices count back from the end, which saves writing
    // len(a) - 1 everywhere.
    if (index < 0) index += (long)array->items.size();
    if (index < 0 || index >= (long)array->items.size()) {
      return runtimeErrorAs("index",
                            "Array index %ld out of range for length %zu.",
                            (long)raw, array->items.size());
    }
    Value result = array->items[(size_t)index];
    pop();
    pop();
    push(result);
    return true;
  }

  if (isMap(target)) {
    Value result;
    if (!asMap(target)->entries.get(indexValue, &result)) result = nilValue();
    pop();
    pop();
    push(result);
    return true;
  }

  if (isString(target)) {
    if (!isNumber(indexValue)) {
      return runtimeErrorAs("type", "String index must be a number, got %s.",
                            valueTypeName(indexValue));
    }
    ObjString* string = asString(target);
    long index = (long)asNumber(indexValue);
    if (index < 0) index += (long)string->length;
    if (index < 0 || index >= (long)string->length) {
      return runtimeErrorAs("index",
                            "String index out of range for length %zu.",
                            string->length);
    }
    ObjString* result = runtime_.copyString(string->chars + index, 1);
    pop();
    pop();
    push(objValue((Obj*)result));
    return true;
  }

  return runtimeErrorAs("type", "Cannot index a value of type %s.",
                        valueTypeName(target));
}

bool VM::setIndex() {
  Value value = peek(0);
  Value indexValue = peek(1);
  Value target = peek(2);

  if (isArray(target)) {
    if (!isNumber(indexValue)) {
      return runtimeErrorAs("type", "Array index must be a number, got %s.",
                            valueTypeName(indexValue));
    }
    ObjArray* array = asArray(target);
    long index = (long)asNumber(indexValue);
    if (index < 0) index += (long)array->items.size();
    if (index < 0 || index >= (long)array->items.size()) {
      return runtimeErrorAs("index",
                            "Array index %ld out of range for length %zu.",
                            index, array->items.size());
    }
    array->items[(size_t)index] = value;
  } else if (isMap(target)) {
    if (!isHashableKey(indexValue)) {
      return runtimeErrorAs(
          "key",
          "A map key must be a string, number, boolean, nil, enum member "
          "or instance, got %s.",
          valueTypeName(indexValue));
    }
    asMap(target)->entries.set(indexValue, value);
  } else {
    return runtimeErrorAs("type",
                          "Cannot assign by index into a value of type %s.",
                          valueTypeName(target));
  }

  pop();
  pop();
  pop();
  push(value);
  return true;
}

bool VM::spawnTask(int argCount) {
  Value callee = peek(argCount);
  ObjTask* task = runtime_.newTask();
  // Registering first makes the task a root, so the argument copy below
  // cannot be collected part way through.
  runtime_.registerTask(task);
  task->callee = callee;
  task->args.reserve((size_t)argCount);
  for (int i = argCount - 1; i >= 0; i--) task->args.push_back(peek(i));

  stackTop_ -= argCount + 1;
  push(objValue((Obj*)task));

  task->thread = new std::thread(taskMain, &runtime_, task);
  return true;
}

bool VM::importModule(ObjString* path) {
  // Relative imports resolve against the importing file, not the working
  // directory, so a module can be moved without editing its imports.
  std::string base = directoryOf(std::string(currentModule()->path->chars,
                                             currentModule()->path->length));
  std::string request(path->chars, path->length);
  std::string resolved = absolutePath(joinPath(base, request));

  // A path that is not next to the importing file is looked for on the
  // library search path, so a shared library of Red code does not have to
  // be reached with a chain of "..". docs/libraries.md describes the
  // order.
  if (!fileExists(resolved) && !request.empty() && request.front() != '/') {
    std::string found = findOnLibraryPath(request);
    if (!found.empty()) resolved = found;
  }
  ObjString* key = runtime_.internString(resolved);
  // Rooted for the whole function: the interner is weak, and everything
  // below allocates.
  GCRoot keyRoot(runtime_, (Obj*)key);

  Value cached;
  if (runtime_.modules.get(key, &cached)) {
    // Already loaded, or still loading because of an import cycle. Either
    // way the module object itself is what the importer needs.
    push(cached);
    return true;
  }

  std::string source;
  if (!readFile(resolved, &source)) {
    return runtimeErrorAs("import", "Cannot open module '%s'.",
                          resolved.c_str());
  }

  std::string name = resolved.substr(resolved.find_last_of('/') + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);

  ObjString* nameString = runtime_.internString(name);
  GCRoot nameRoot(runtime_, (Obj*)nameString);
  ObjModule* module = runtime_.newModule(nameString, key);
  GCRoot moduleRoot(runtime_, (Obj*)module);
  runtime_.modules.set(key, objValue((Obj*)module));

  // A module may be compiled ahead of time, the same as a program. That
  // is worth doing for a large library that many programs import, because
  // loading a chunk skips the compiler entirely.
  ObjFunction* function;
  if (looksCompiled(source)) {
    std::string reason;
    function = readCompiled(runtime_, source, module, &reason);
    if (function == nullptr) {
      runtime_.modules.remove(key);
      return runtimeErrorAs("import", "Cannot load module '%s': %s",
                            resolved.c_str(), reason.c_str());
    }
  } else {
    function = compile(runtime_, source, module);
    if (function == nullptr) {
      runtime_.modules.remove(key);
      return runtimeErrorAs("import", "Module '%s' failed to compile.",
                            resolved.c_str());
    }
  }

  // callAndRun expects the callee to be on the stack already, the same
  // way a compiled call site leaves it.
  ObjClosure* closure = runtime_.newClosure(function);
  push(objValue((Obj*)closure));
  Value ignored;
  InterpretResult status = callAndRun(objValue((Obj*)closure), 0, &ignored);
  if (status != InterpretResult::Ok) {
    runtime_.modules.remove(key);
    return raise(lastError);
  }

  module->loaded = true;
  push(objValue((Obj*)module));
  return true;
}

// ---------------------------------------------------------------------
// the interpreter loop

// Picks which of the two loops to run. Everything below is shared; the
// template parameter decides whether the debugger and the tracer exist
// at all in the instantiation.
InterpretResult VM::run(int baseFrame) {
  if (runtime_.debugger != nullptr || runtime_.traceExecution) {
    return runLoop<true>(baseFrame);
  }
  return runLoop<false>(baseFrame);
}

template <bool Instrumented>
InterpretResult VM::runLoop(int baseFrame) {
  int previousBase = baseFrame_;
  baseFrame_ = baseFrame;
  CallFrame* frame = &frames_[frameCount_ - 1];

#define READ_BYTE() (*frame->ip++)
#define READ_SHORT() \
  (frame->ip += 2, (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants[READ_SHORT()])
#define READ_STRING() asString(READ_CONSTANT())
#define RETURN_WITH(status)   \
  {                           \
    baseFrame_ = previousBase; \
    return (status);          \
  }
// Reports a runtime fault. Either a catch block takes over, in which case
// the cached frame pointer has to be refreshed, or the error leaves this
// run loop entirely.
#define FAULT(...)                                                   \
  {                                                                  \
    if (!runtimeError(__VA_ARGS__)) RETURN_WITH(InterpretResult::RuntimeError) \
    frame = &frames_[frameCount_ - 1];                               \
    break;                                                           \
  }
#define FAULT_AS(kind, ...)                                          \
  {                                                                  \
    if (!runtimeErrorAs(kind, __VA_ARGS__))                          \
      RETURN_WITH(InterpretResult::RuntimeError)                     \
    frame = &frames_[frameCount_ - 1];                               \
    break;                                                           \
  }
#define CHECK(ok)                                                    \
  {                                                                  \
    if (!(ok)) {                                                     \
      if (frameCount_ == 0 || handlers_.empty() ||                   \
          handlers_.back().frameIndex < baseFrame_) {                \
        RETURN_WITH(InterpretResult::RuntimeError)                   \
      }                                                              \
      frame = &frames_[frameCount_ - 1];                             \
      break;                                                         \
    }                                                                \
    frame = &frames_[frameCount_ - 1];                               \
  }

  for (;;) {
    if constexpr (Instrumented) {
      if (runtime_.debugger != nullptr) {
        runtime_.debugger->beforeInstruction(*this, frame);
      }
      if (runtime_.traceExecution) {
        std::printf("          ");
        for (Value* slot = stack_; slot < stackTop_; slot++) {
          std::printf("[ %s ]", valueToDisplay(*slot).c_str());
        }
        std::printf("\n");
        disassembleInstruction(
            frame->closure->function->chunk,
            (size_t)(frame->ip - frame->closure->function->chunk.code.data()));
      }
    }

    uint8_t instruction = READ_BYTE();
    switch (instruction) {
      case OP_CONSTANT: push(READ_CONSTANT()); break;
      case OP_NIL: push(nilValue()); break;
      case OP_TRUE: push(boolValue(true)); break;
      case OP_FALSE: push(boolValue(false)); break;
      case OP_POP: pop(); break;
      case OP_DUP: push(peek(0)); break;
      case OP_DUP2: {
        // Used by compound assignment to a subscript, which needs both
        // the target and the index twice.
        Value first = peek(1);
        Value second = peek(0);
        push(first);
        push(second);
        break;
      }

      case OP_GET_LOCAL: push(frame->slots[READ_BYTE()]); break;
      case OP_SET_LOCAL: frame->slots[READ_BYTE()] = peek(0); break;

      case OP_GET_GLOBAL: {
        ObjString* name = READ_STRING();
        Value value;
        if (currentModule()->globals.get(name, &value)) {
          push(value);
        } else if (runtime_.builtins.get(name, &value)) {
          push(value);
        } else {
          FAULT_AS("name", "Undefined variable '%s'.", name->chars)
        }
        break;
      }
      case OP_DEFINE_GLOBAL: {
        ObjString* name = READ_STRING();
        currentModule()->globals.set(name, peek(0));
        pop();
        break;
      }
      case OP_SET_GLOBAL: {
        ObjString* name = READ_STRING();
        if (currentModule()->globals.set(name, peek(0))) {
          // set() reports that the key was new, which means the program
          // assigned to something it never declared.
          currentModule()->globals.remove(name);
          FAULT_AS("name", "Undefined variable '%s'.", name->chars)
        }
        break;
      }

      case OP_GET_UPVALUE:
        push(*frame->closure->upvalues[READ_BYTE()]->location);
        break;
      case OP_SET_UPVALUE:
        *frame->closure->upvalues[READ_BYTE()]->location = peek(0);
        break;

      case OP_GET_PROPERTY: {
        Value receiver = peek(0);
        ObjString* name = READ_STRING();
        if (isInstance(receiver)) {
          ObjInstance* instance = asInstance(receiver);
          Value value;
          if (instance->fields.get(name, &value)) {
            pop();
            push(value);
            break;
          }
          CHECK(bindMethod(instance->klass, name))
          break;
        }
        if (isObjType(receiver, ObjType::Module)) {
          Value value;
          if (!asModule(receiver)->globals.get(name, &value)) {
            FAULT_AS("name", "Undefined name '%s' in module.", name->chars)
          }
          pop();
          push(value);
          break;
        }
        if (isError(receiver)) {
          ObjError* error = asError(receiver);
          std::string field(name->chars, name->length);
          if (field == "message") {
            pop();
            push(objValue((Obj*)error->message));
            break;
          }
          if (field == "trace") {
            pop();
            push(objValue((Obj*)error->trace));
            break;
          }
          if (field == "kind") {
            pop();
            push(objValue((Obj*)error->kind));
            break;
          }
          if (field == "payload") {
            pop();
            push(error->payload);
            break;
          }
        }
        if (isEnum(receiver)) {
          Value member;
          if (asEnum(receiver)->members.get(name, &member)) {
            pop();
            push(member);
            break;
          }
          // values() and from() are provided as methods, so fall through
          // to the built-in method lookup before reporting.
          if (lookupBuiltinMethod(runtime_, receiver, name) == nullptr) {
            FAULT_AS("name", "Enum %s has no member '%s'.",
                     asEnum(receiver)->name->chars, name->chars)
          }
        }
        if (isEnumMember(receiver)) {
          ObjEnumMember* member = asEnumMember(receiver);
          std::string field(name->chars, name->length);
          if (field == "name") {
            pop();
            push(objValue((Obj*)member->name));
            break;
          }
          if (field == "value") {
            pop();
            push(numberValue(member->value));
            break;
          }
          // Not called "enum", because that is a keyword and so could
          // never be written after a dot.
          if (field == "owner") {
            pop();
            push(objValue((Obj*)member->parent));
            break;
          }
        }
        if (getBuiltinProperty(receiver, name)) break;
        FAULT_AS("name", "Type %s has no property '%s'.",
                 valueTypeName(receiver), name->chars)
      }

      case OP_SET_PROPERTY: {
        Value receiver = peek(1);
        ObjString* name = READ_STRING();
        if (!isInstance(receiver)) {
          FAULT_AS("type", "Only instances have assignable fields, got %s.",
                   valueTypeName(receiver))
        }
        asInstance(receiver)->fields.set(name, peek(0));
        Value value = pop();
        pop();
        push(value);
        break;
      }

      case OP_GET_SUPER: {
        ObjString* name = READ_STRING();
        ObjClass* superclass = asClass(pop());
        CHECK(bindMethod(superclass, name))
        break;
      }

      case OP_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(boolValue(equal(a, b)));
        break;
      }
      case OP_NOT_EQUAL: {
        Value b = pop();
        Value a = pop();
        push(boolValue(!equal(a, b)));
        break;
      }

      case OP_GREATER:
      case OP_GREATER_EQUAL:
      case OP_LESS:
      case OP_LESS_EQUAL: {
        if (isString(peek(0)) && isString(peek(1))) {
          ObjString* b = asString(pop());
          ObjString* a = asString(pop());
          int order = std::memcmp(a->chars, b->chars,
                                  a->length < b->length ? a->length : b->length);
          if (order == 0) {
            order = a->length < b->length ? -1 : (a->length > b->length ? 1 : 0);
          }
          bool result = instruction == OP_GREATER        ? order > 0
                        : instruction == OP_GREATER_EQUAL ? order >= 0
                        : instruction == OP_LESS          ? order < 0
                                                          : order <= 0;
          push(boolValue(result));
          break;
        }
        if (!isNumber(peek(0)) || !isNumber(peek(1))) {
          FAULT_AS("type",
                   "Comparison needs two numbers or two strings, got %s and %s.",
                   valueTypeName(peek(1)), valueTypeName(peek(0)))
        }
        double b = asNumber(pop());
        double a = asNumber(pop());
        bool result = instruction == OP_GREATER        ? a > b
                      : instruction == OP_GREATER_EQUAL ? a >= b
                      : instruction == OP_LESS          ? a < b
                                                        : a <= b;
        push(boolValue(result));
        break;
      }

      case OP_ADD: {
        if (isString(peek(0)) && isString(peek(1))) {
          concatenate();
          break;
        }
        if (isNumber(peek(0)) && isNumber(peek(1))) {
          double b = asNumber(pop());
          double a = asNumber(pop());
          push(numberValue(a + b));
          break;
        }
        FAULT_AS("type", "Cannot add %s and %s.", valueTypeName(peek(1)),
                 valueTypeName(peek(0)))
      }

      case OP_SUBTRACT:
      case OP_MULTIPLY:
      case OP_DIVIDE:
      case OP_MODULO: {
        if (!isNumber(peek(0)) || !isNumber(peek(1))) {
          FAULT_AS("type", "Arithmetic needs two numbers, got %s and %s.",
                   valueTypeName(peek(1)), valueTypeName(peek(0)))
        }
        double b = asNumber(peek(0));
        double a = asNumber(peek(1));
        if ((instruction == OP_DIVIDE || instruction == OP_MODULO) && b == 0) {
          FAULT_AS("zero-division", "Division by zero.")
        }
        pop();
        pop();
        switch (instruction) {
          case OP_SUBTRACT: push(numberValue(a - b)); break;
          case OP_MULTIPLY: push(numberValue(a * b)); break;
          case OP_DIVIDE: push(numberValue(a / b)); break;
          default: push(numberValue(std::fmod(a, b))); break;
        }
        break;
      }

      case OP_NEGATE: {
        if (!isNumber(peek(0))) {
          FAULT_AS("type", "Cannot negate a value of type %s.",
                   valueTypeName(peek(0)))
        }
        push(numberValue(-asNumber(pop())));
        break;
      }
      case OP_NOT: push(boolValue(isFalsey(pop()))); break;

      case OP_BIT_AND:
      case OP_BIT_OR:
      case OP_BIT_XOR:
      case OP_SHIFT_LEFT:
      case OP_SHIFT_RIGHT: {
        if (!isNumber(peek(0)) || !isNumber(peek(1))) {
          FAULT_AS("type", "Bitwise operators need two numbers, got %s and %s.",
                   valueTypeName(peek(1)), valueTypeName(peek(0)))
        }
        int32_t right = toInt32(asNumber(pop()));
        int32_t left = toInt32(asNumber(pop()));
        int32_t result;
        switch (instruction) {
          case OP_BIT_AND: result = left & right; break;
          case OP_BIT_OR: result = left | right; break;
          case OP_BIT_XOR: result = left ^ right; break;
          // The shift count is masked, so shifting by 32 or more is
          // defined rather than left to the host.
          case OP_SHIFT_LEFT:
            result = (int32_t)((uint32_t)left << (right & 31));
            break;
          default: result = left >> (right & 31); break;
        }
        push(numberValue((double)result));
        break;
      }
      case OP_BIT_NOT: {
        if (!isNumber(peek(0))) {
          FAULT_AS("type", "Cannot apply '~' to a value of type %s.",
                   valueTypeName(peek(0)))
        }
        push(numberValue((double)~toInt32(asNumber(pop()))));
        break;
      }

      case OP_TO_STRING: {
        // Through stringify, so "${p}" goes via the class's own str()
        // the same way print(p) does.
        ObjString* text = runtime_.copyString(stringify(peek(0)));
        pop();
        push(objValue((Obj*)text));
        break;
      }

      case OP_JUMP: {
        uint16_t offset = READ_SHORT();
        frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = READ_SHORT();
        if (isFalsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_JUMP_IF_TRUE: {
        uint16_t offset = READ_SHORT();
        if (!isFalsey(peek(0))) frame->ip += offset;
        break;
      }
      case OP_LOOP: {
        uint16_t offset = READ_SHORT();
        frame->ip -= offset;
        break;
      }

      case OP_CALL: {
        int argCount = READ_BYTE();
        CHECK(callValue(peek(argCount), argCount))
        break;
      }
      case OP_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        CHECK(invoke(method, argCount))
        break;
      }
      case OP_SUPER_INVOKE: {
        ObjString* method = READ_STRING();
        int argCount = READ_BYTE();
        ObjClass* superclass = asClass(pop());
        CHECK(invokeFromClass(superclass, method, argCount))
        break;
      }

      case OP_CLOSURE: {
        ObjFunction* function = asFunction(READ_CONSTANT());
        ObjClosure* closure = runtime_.newClosure(function);
        push(objValue((Obj*)closure));
        for (int i = 0; i < closure->upvalueCount; i++) {
          uint8_t isLocal = READ_BYTE();
          uint8_t index = READ_BYTE();
          closure->upvalues[i] = isLocal ? captureUpvalue(frame->slots + index)
                                         : frame->closure->upvalues[index];
        }
        break;
      }
      case OP_CLOSE_UPVALUE:
        closeUpvalues(stackTop_ - 1);
        pop();
        break;

      case OP_RETURN: {
        Value result = pop();
        closeUpvalues(frame->slots);
        frameCount_--;
        while (!handlers_.empty() &&
               handlers_.back().frameIndex >= frameCount_) {
          handlers_.pop_back();
        }
        stackTop_ = frame->slots;
        push(result);
        if (frameCount_ == baseFrame_) RETURN_WITH(InterpretResult::Ok)
        frame = &frames_[frameCount_ - 1];
        break;
      }

      case OP_CLASS:
        push(objValue((Obj*)runtime_.newClass(READ_STRING())));
        break;
      case OP_INHERIT: {
        Value superclass = peek(1);
        if (!isClass(superclass)) {
          FAULT_AS("type", "A superclass must be a class, got %s.",
                   valueTypeName(superclass))
        }
        // Copy down rather than chain at lookup time. Method dispatch then
        // costs one table probe no matter how deep the hierarchy is. The
        // link is still kept, so a value can be tested against a whole
        // hierarchy.
        asClass(peek(0))->superclass = asClass(superclass);
        asClass(peek(0))->methods.addAll(asClass(superclass)->methods);
        pop();
        break;
      }
      case OP_METHOD: defineMethod(READ_STRING()); break;

      case OP_ARRAY: {
        uint16_t count = READ_SHORT();
        ObjArray* array = runtime_.newArray();
        GCRoot arrayRoot(runtime_, (Obj*)array);
        array->items.assign(stackTop_ - count, stackTop_);
        stackTop_ -= count;
        push(objValue((Obj*)array));
        break;
      }
      case OP_MAP: {
        uint16_t count = READ_SHORT();
        ObjMap* map = runtime_.newMap();
        GCRoot mapRoot(runtime_, (Obj*)map);
        bool badKey = false;
        for (uint16_t i = 0; i < count; i++) {
          Value* pair = stackTop_ - (count - i) * 2;
          if (!isHashableKey(pair[0])) {
            badKey = true;
            break;
          }
          map->entries.set(pair[0], pair[1]);
        }
        if (badKey) {
          FAULT("A map key must be a string, number, boolean, nil, enum "
                "member.")
        }
        stackTop_ -= count * 2;
        push(objValue((Obj*)map));
        break;
      }
      case OP_GET_INDEX: CHECK(getIndex()) break;
      case OP_SET_INDEX: CHECK(setIndex()) break;

      case OP_TRY_BEGIN: {
        uint16_t offset = READ_SHORT();
        handlers_.push_back({frameCount_ - 1, stackTop_ - stack_,
                             frame->ip + offset});
        break;
      }
      case OP_TRY_END:
        if (!handlers_.empty()) handlers_.pop_back();
        break;
      case OP_THROW: {
        Value thrown = pop();
        if (!isError(thrown)) {
          // Anything can be thrown. A value that is not already an error
          // is wrapped, so catch always receives something with a
          // message, a trace and a kind.
          GCRoot thrownRoot(runtime_, thrown);
          std::string kind = "user";
          std::string message = valueToString(thrown);
          if (isInstance(thrown)) {
            // An instance takes its class name as its kind, which is what
            // lets `catch (e: ParseError)` work.
            ObjInstance* instance = asInstance(thrown);
            kind.assign(instance->klass->name->chars,
                        instance->klass->name->length);
            Value stored;
            if (instance->fields.get(runtime_.messageString, &stored)) {
              message = valueToString(stored);
            }
          }
          thrown = makeError(kind.c_str(), message, thrown);
        }
        if (!raise(thrown)) RETURN_WITH(InterpretResult::RuntimeError)
        frame = &frames_[frameCount_ - 1];
        break;
      }

      case OP_SPAWN: {
        int argCount = READ_BYTE();
        CHECK(spawnTask(argCount))
        break;
      }

      case OP_IMPORT: {
        ObjString* path = READ_STRING();
        CHECK(importModule(path))
        break;
      }

      case OP_ITER_PREP: {
        Value subject = peek(0);
        if (isArray(subject)) break;
        if (isMap(subject)) {
          // A snapshot of the keys, so changing the map while walking it
          // cannot disturb the walk.
          ObjArray* keys = runtime_.newArray();
          GCRoot keysRoot(runtime_, (Obj*)keys);
          for (const ValueEntry& slot : asMap(subject)->entries.slots()) {
            if (slot.used) keys->items.push_back(slot.key);
          }
          pop();
          push(objValue((Obj*)keys));
          break;
        }
        if (isSet(subject)) {
          ObjArray* items = runtime_.newArray();
          GCRoot itemsRoot(runtime_, (Obj*)items);
          for (const ValueEntry& slot : asSet(subject)->entries.slots()) {
            if (slot.used) items->items.push_back(slot.key);
          }
          pop();
          push(objValue((Obj*)items));
          break;
        }
        if (isString(subject)) {
          // Characters, not bytes, so a loop over text holds together
          // for input that is not ASCII. bytes() is there for when the
          // string is data rather than text.
          ObjString* text = asString(subject);
          ObjArray* characters = runtime_.newArray();
          GCRoot charactersRoot(runtime_, (Obj*)characters);
          size_t i = 0;
          while (i < text->length) {
            uint32_t codePoint;
            size_t width = decodeUtf8(text->chars, text->length, i, &codePoint);
            characters->items.push_back(
                objValue((Obj*)runtime_.copyString(text->chars + i, width)));
            i += width;
          }
          pop();
          push(objValue((Obj*)characters));
          break;
        }
        FAULT_AS("type", "Cannot walk a value of type %s.",
                 valueTypeName(subject))
      }

      case OP_ITER_NEXT: {
        uint8_t sequenceSlot = READ_BYTE();
        uint8_t indexSlot = READ_BYTE();
        uint16_t offset = READ_SHORT();
        ObjArray* sequence = asArray(frame->slots[sequenceSlot]);
        size_t index = (size_t)asNumber(frame->slots[indexSlot]);
        // The length is read every time, so a loop over an array that
        // shrinks underneath it stops rather than reading past the end.
        if (index >= sequence->items.size()) {
          frame->ip += offset;
          break;
        }
        push(sequence->items[index]);
        frame->slots[indexSlot] = numberValue((double)(index + 1));
        break;
      }

      case OP_CATCH_MATCHES: {
        // Both operands are consumed. The compiler pushes a copy of the
        // error for each clause precisely so this can take it.
        Value filter = pop();
        Value caught = pop();
        ObjError* error = asError(caught);
        if (isString(filter)) {
          // Kinds are interned, so this is a pointer compare.
          push(boolValue(error->kind == asString(filter)));
          break;
        }
        if (isClass(filter)) {
          bool matches = false;
          if (isInstance(error->payload)) {
            for (ObjClass* klass = asInstance(error->payload)->klass;
                 klass != nullptr; klass = klass->superclass) {
              if (klass == asClass(filter)) {
                matches = true;
                break;
              }
            }
          }
          push(boolValue(matches));
          break;
        }
        FAULT_AS("type",
                 "A catch filter must be a string or a class, got %s.",
                 valueTypeName(filter))
      }

      case OP_DESTRUCTURE_INDEX: {
        uint16_t index = READ_SHORT();
        Value subject = peek(0);
        Value result = nilValue();
        if (isArray(subject)) {
          ObjArray* array = asArray(subject);
          if (index < array->items.size()) result = array->items[index];
        } else if (isString(subject)) {
          ObjString* text = asString(subject);
          if (index < text->length) {
            result =
                objValue((Obj*)runtime_.copyString(text->chars + index, 1));
          }
        } else {
          FAULT_AS("type", "Cannot take elements from a value of type %s.",
                   valueTypeName(subject))
        }
        pop();
        push(result);
        break;
      }

      case OP_DESTRUCTURE_REST: {
        uint16_t index = READ_SHORT();
        Value subject = peek(0);
        if (!isArray(subject)) {
          FAULT_AS("type", "A rest pattern needs an array, got %s.",
                   valueTypeName(subject))
        }
        ObjArray* rest = runtime_.newArray();
        GCRoot restRoot(runtime_, (Obj*)rest);
        ObjArray* source = asArray(subject);
        if (index < source->items.size()) {
          rest->items.assign(source->items.begin() + index,
                             source->items.end());
        }
        pop();
        push(objValue((Obj*)rest));
        break;
      }

      case OP_DESTRUCTURE_FIELD: {
        ObjString* name = READ_STRING();
        Value subject = peek(0);
        Value result = nilValue();
        if (isMap(subject)) {
          asMap(subject)->entries.get(objValue((Obj*)name), &result);
        } else if (isInstance(subject)) {
          asInstance(subject)->fields.get(name, &result);
        } else if (isObjType(subject, ObjType::Module)) {
          if (!asModule(subject)->globals.get(name, &result)) {
            FAULT_AS("name", "Module has no name '%s'.", name->chars)
          }
        } else if (isEnum(subject)) {
          if (!asEnum(subject)->members.get(name, &result)) {
            FAULT_AS("name", "Enum %s has no member '%s'.",
                     asEnum(subject)->name->chars, name->chars)
          }
        } else {
          FAULT_AS("type", "Cannot take fields from a value of type %s.",
                   valueTypeName(subject))
        }
        pop();
        push(result);
        break;
      }

      case OP_JUMP_IF_ARG: {
        uint8_t index = READ_BYTE();
        uint16_t offset = READ_SHORT();
        if ((int)index < frame->argCount) frame->ip += offset;
        break;
      }

      default:
        FAULT("Unknown opcode %d.", instruction)
    }
  }

#undef CHECK
#undef FAULT_AS
#undef FAULT
#undef RETURN_WITH
#undef READ_STRING
#undef READ_CONSTANT
#undef READ_SHORT
#undef READ_BYTE
}

InterpretResult VM::callAndRun(Value callee, int argCount, Value* result) {
  int base = frameCount_;
  if (!callValue(callee, argCount)) {
    *result = nilValue();
    return InterpretResult::RuntimeError;
  }
  // A native callee finishes inside callValue and leaves its result on the
  // stack, so there is no new frame to run.
  if (frameCount_ == base) {
    *result = pop();
    return InterpretResult::Ok;
  }
  InterpretResult status = run(base);
  *result = status == InterpretResult::Ok ? pop() : nilValue();
  return status;
}

InterpretResult VM::runFunction(ObjFunction* function) {
  GCRoot functionRoot(runtime_, (Obj*)function);
  ObjClosure* closure = runtime_.newClosure(function);
  push(objValue((Obj*)closure));
  if (!call(closure, 0)) return InterpretResult::RuntimeError;
  InterpretResult status = run(0);
  if (status == InterpretResult::Ok) pop();
  return status;
}

InterpretResult VM::interpret(const std::string& source, ObjModule* module) {
  ObjFunction* function = compile(runtime_, source, module);
  if (function == nullptr) return InterpretResult::CompileError;
  return runFunction(function);
}

}  // namespace red
