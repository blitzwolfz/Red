#include "builtins.h"

#include <cstdlib>

#include "../util.h"
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

std::vector<std::string> librarySearchPaths() {
  std::vector<std::string> directories;

  // RED_PATH first, so a project can put its own version of a library
  // ahead of the one that shipped with the interpreter.
  const char* search = std::getenv("RED_PATH");
  if (search != nullptr) {
    std::string entries(search);
    size_t start = 0;
    while (start <= entries.size()) {
      size_t end = entries.find(':', start);
      if (end == std::string::npos) end = entries.size();
      std::string directory = entries.substr(start, end - start);
      if (!directory.empty()) directories.push_back(directory);
      start = end + 1;
    }
  }

  // Then what ships with this interpreter. `lib/red` is the installed
  // layout, where the binary is in `prefix/bin`; `lib` is the repository
  // layout, where it is in `build`. The binary's own directory comes last
  // and is where a freshly built extension lands.
  if (!g_executablePath.empty()) {
    std::string base = directoryOf(absolutePath(g_executablePath));
    directories.push_back(joinPath(directoryOf(base), "lib/red"));
    directories.push_back(joinPath(directoryOf(base), "lib"));
    directories.push_back(base);
  }
  return directories;
}

std::string findOnLibraryPath(const std::string& request) {
  if (request.empty()) return "";
  if (request.front() == '/') {
    return fileExists(request) ? request : "";
  }
  for (const std::string& directory : librarySearchPaths()) {
    std::string candidate = absolutePath(joinPath(directory, request));
    if (fileExists(candidate)) return candidate;
  }
  return "";
}

void defineGlobalFn(Runtime& runtime, const char* name, NativeFn fn,
                    int arity) {
  ObjNative* native = runtime.newNative(fn, name, arity);
  GCRoot nativeRoot(runtime, (Obj*)native);
  runtime.builtins.set(runtime.internString(name), objValue((Obj*)native));
}

void defineGlobalValue(Runtime& runtime, const char* name, Value value) {
  runtime.builtins.set(runtime.internString(name), value);
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
  installProcess(runtime);
  installRegex(runtime);
  installLegacy(runtime);
}

}  // namespace red
