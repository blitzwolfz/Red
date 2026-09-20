// Process and operating system access.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>
#include <thread>

#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

Value nativeEnv(VM& vm, int argCount, Value* args) {
  if (!isString(args[0])) {
    return vm.fail("env() expects a string, got %s.", valueTypeName(args[0]));
  }
  const char* value =
      std::getenv(std::string(asString(args[0])->chars,
                              asString(args[0])->length).c_str());
  if (value == nullptr) return argCount > 1 ? args[1] : nilValue();
  return objValue((Obj*)vm.runtime().internString(value));
}

Value nativeSetEnv(VM& vm, int, Value* args) {
  if (!isString(args[0]) || !isString(args[1])) {
    return vm.fail("set_env() expects two strings.");
  }
  std::string name(asString(args[0])->chars, asString(args[0])->length);
  std::string value(asString(args[1])->chars, asString(args[1])->length);
  return boolValue(::setenv(name.c_str(), value.c_str(), 1) == 0);
}

Value nativeArgs(VM& vm, int, Value*) {
  ObjArray* array = vm.runtime().newArray();
  GCRoot arrayRoot(vm.runtime(), (Obj*)array);
  for (const std::string& arg : vm.runtime().scriptArgs) {
    array->items.push_back(
        objValue((Obj*)vm.runtime().copyString(arg.data(), arg.size())));
  }
  return objValue((Obj*)array);
}

Value nativeExit(VM& vm, int argCount, Value* args) {
  int code = 0;
  if (argCount > 0) {
    if (!isNumber(args[0])) {
      return vm.fail("exit() expects a number, got %s.", valueTypeName(args[0]));
    }
    code = (int)asNumber(args[0]);
  }
  // Flush before leaving, because other tasks may still be mid write and
  // exit() does not unwind them.
  std::fflush(stdout);
  std::fflush(stderr);
  std::_Exit(code);
  return nilValue();
}

Value nativeTime(VM&, int, Value*) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return numberValue((double)now.tv_sec + (double)now.tv_nsec / 1e9);
}

Value nativeCwd(VM& vm, int, Value*) {
  char buffer[4096];
  if (::getcwd(buffer, sizeof(buffer)) == nullptr) return nilValue();
  return objValue((Obj*)vm.runtime().internString(buffer));
}

Value nativeExists(VM& vm, int, Value* args) {
  if (!isString(args[0])) {
    return vm.fail("exists() expects a string, got %s.", valueTypeName(args[0]));
  }
  struct stat info;
  std::string path(asString(args[0])->chars, asString(args[0])->length);
  return boolValue(::stat(path.c_str(), &info) == 0);
}

Value nativePlatform(VM& vm, int, Value*) {
#if defined(__APPLE__)
  return objValue((Obj*)vm.runtime().internString("darwin"));
#elif defined(__linux__)
  return objValue((Obj*)vm.runtime().internString("linux"));
#else
  return objValue((Obj*)vm.runtime().internString("unknown"));
#endif
}

Value nativeCpuCount(VM&, int, Value*) {
  unsigned count = std::thread::hardware_concurrency();
  return numberValue(count == 0 ? 1 : (double)count);
}

Value nativeGCInfo(VM& vm, int, Value*) {
  Runtime& rt = vm.runtime();
  ObjMap* info = rt.newMap();
  GCRoot infoRoot(rt, (Obj*)info);
  info->entries.set(objValue((Obj*)rt.internString("bytes")),
                    numberValue((double)rt.bytesAllocated));
  info->entries.set(objValue((Obj*)rt.internString("next")),
                    numberValue((double)rt.nextGC));
  info->entries.set(objValue((Obj*)rt.internString("collections")),
                    numberValue((double)rt.collections));
  info->entries.set(objValue((Obj*)rt.internString("peak")),
                    numberValue((double)rt.peakBytes));
  return objValue((Obj*)info);
}

Value nativeCollect(VM& vm, int, Value*) {
  vm.runtime().collectGarbage();
  return nilValue();
}

}  // namespace

void installOS(Runtime& runtime) {
  defineGlobalFn(runtime, "env", nativeEnv, -1);
  defineGlobalFn(runtime, "set_env", nativeSetEnv, 2);
  defineGlobalFn(runtime, "args", nativeArgs, 0);
  defineGlobalFn(runtime, "exit", nativeExit, -1);
  defineGlobalFn(runtime, "time", nativeTime, 0);
  defineGlobalFn(runtime, "cwd", nativeCwd, 0);
  defineGlobalFn(runtime, "exists", nativeExists, 1);
  defineGlobalFn(runtime, "platform", nativePlatform, 0);
  defineGlobalFn(runtime, "cpu_count", nativeCpuCount, 0);
  defineGlobalFn(runtime, "gc_info", nativeGCInfo, 0);
  defineGlobalFn(runtime, "collect", nativeCollect, 0);
}

}  // namespace red
