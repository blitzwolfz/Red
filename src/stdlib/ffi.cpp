// Loading functions from shared libraries.
//
// An extension is a shared library that exports functions matching the
// ForeignFn signature in object.h. ffi/red_ffi.h describes that contract
// in plain C, together with the helper functions exported below.
#include <dlfcn.h>

#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

Value nativeFfiOpen(VM& vm, int, Value* args) {
  if (!isString(args[0])) {
    return vm.failAs("type", "ffi_open() expects a path string, got %s.",
                   valueTypeName(args[0]));
  }
  ObjString* path = asString(args[0]);
  void* handle = ::dlopen(std::string(path->chars, path->length).c_str(),
                          RTLD_NOW | RTLD_LOCAL);
  if (handle == nullptr) {
    return vm.failAs("ffi", "ffi_open() failed: %s", ::dlerror());
  }
  return objValue((Obj*)vm.runtime().newNativeLib(handle, path));
}

Value libSym(VM& vm, int argCount, Value* args) {
  ObjNativeLib* lib = asNativeLib(args[0]);
  if (lib->handle == nullptr) return vm.failAs("ffi", "sym() on a closed library.");
  if (!isString(args[1])) {
    return vm.failAs("type", "sym() expects a symbol name string.");
  }
  std::string name(asString(args[1])->chars, asString(args[1])->length);

  ::dlerror();  // clear any stale error before the lookup
  void* symbol = ::dlsym(lib->handle, name.c_str());
  const char* error = ::dlerror();
  if (error != nullptr) {
    return vm.failAs("ffi", "sym('%s') failed: %s", name.c_str(), error);
  }

  int arity = argCount > 2 && isNumber(args[2]) ? (int)asNumber(args[2]) : -1;
  ObjNative* native = vm.runtime().newNative(nullptr, name, arity);
  native->foreign = (ForeignFn)symbol;
  return objValue((Obj*)native);
}

Value libClose(VM&, int, Value* args) {
  ObjNativeLib* lib = asNativeLib(args[0]);
  if (lib->handle != nullptr) {
    ::dlclose(lib->handle);
    lib->handle = nullptr;
  }
  return nilValue();
}

}  // namespace

void installFFI(Runtime& runtime) {
  defineGlobalFn(runtime, "ffi_open", nativeFfiOpen, 1);
  defineMethodFn(runtime, ObjType::NativeLib, "sym", libSym, -1);
  defineMethodFn(runtime, ObjType::NativeLib, "close", libClose, 1);
}

}  // namespace red

// ---------------------------------------------------------------------
// Helpers an extension links against. They are plain C so that an
// extension can be written in C without knowing anything about the C++
// side of the runtime.

extern "C" {

red::Value red_nil(void) { return red::nilValue(); }
red::Value red_bool(int value) { return red::boolValue(value != 0); }
red::Value red_number(double value) { return red::numberValue(value); }

int red_is_nil(red::Value value) { return red::isNil(value) ? 1 : 0; }
int red_is_bool(red::Value value) { return red::isBool(value) ? 1 : 0; }
int red_is_number(red::Value value) { return red::isNumber(value) ? 1 : 0; }
int red_is_string(red::Value value) { return red::isString(value) ? 1 : 0; }

int red_as_bool(red::Value value) { return red::asBool(value) ? 1 : 0; }
double red_as_number(red::Value value) { return red::asNumber(value); }

const char* red_string_chars(red::Value value) {
  return red::isString(value) ? red::asString(value)->chars : "";
}

size_t red_string_length(red::Value value) {
  return red::isString(value) ? red::asString(value)->length : 0;
}

// Allocating a string needs the runtime, which is why every extension
// function receives the context it was called with.
red::Value red_new_string(void* context, const char* chars, size_t length) {
  red::VM* vm = (red::VM*)context;
  return red::objValue((red::Obj*)vm->runtime().copyString(chars, length));
}

// Reports an error back to the interpreter. The extension should return
// immediately afterwards.
red::Value red_fail(void* context, const char* message) {
  red::VM* vm = (red::VM*)context;
  return vm->fail("%s", message);
}

}  // extern "C"
