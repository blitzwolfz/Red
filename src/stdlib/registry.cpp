#include "builtins.h"

#include "../vm.h"

namespace red {

namespace {

// One table per object type. A method call on a built-in type is a single
// probe into the table for that type.
constexpr int kObjTypeCount = (int)ObjType::Error + 1;
Table* g_methodTables = nullptr;
std::string g_executablePath;

}  // namespace

void setExecutablePath(const std::string& path) { g_executablePath = path; }
const std::string& executablePath() { return g_executablePath; }

void defineGlobalFn(Runtime& runtime, const char* name, NativeFn fn,
                    int arity) {
  ObjNative* native = runtime.newNative(fn, name, arity);
  GCRoot nativeRoot(runtime, (Obj*)native);
  runtime.builtins.set(runtime.internString(name), objValue((Obj*)native));
}

void defineMethodFn(Runtime& runtime, ObjType type, const char* name,
                    NativeFn fn, int arity) {
  ObjNative* native = runtime.newNative(fn, name, arity);
  GCRoot nativeRoot(runtime, (Obj*)native);
  g_methodTables[(int)type].set(runtime.internString(name),
                                objValue((Obj*)native));
}

ObjNative* lookupBuiltinMethod(Runtime&, Value receiver, ObjString* name) {
  if (!isObj(receiver) || g_methodTables == nullptr) return nullptr;
  Value method;
  if (!g_methodTables[(int)asObj(receiver)->type].get(name, &method)) {
    return nullptr;
  }
  return asNative(method);
}

void installBuiltins(Runtime& runtime) {
  // One Runtime exists per process, so these tables are created once and
  // live until exit.
  g_methodTables = new Table[kObjTypeCount];
  for (int i = 0; i < kObjTypeCount; i++) {
    runtime.rootTables.push_back(&g_methodTables[i]);
  }

  installCore(runtime);
  installIO(runtime);
  installOS(runtime);
  installNet(runtime);
  installConcurrency(runtime);
  installFFI(runtime);
  installLegacy(runtime);
}

}  // namespace red
