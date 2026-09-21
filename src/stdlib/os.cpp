// Process and operating system access.
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <ctime>
#include <thread>

#include "../util.h"
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

// Where the calling file lives. A library uses these to reach a data file
// or an extension that sits beside it, whatever directory the program
// that imported it was started from.
Value nativeSourcePath(VM& vm, int, Value*) {
  ObjModule* module = vm.currentModule();
  if (module == nullptr) return nilValue();
  return objValue((Obj*)module->path);
}

Value nativeSourceDir(VM& vm, int, Value*) {
  ObjModule* module = vm.currentModule();
  if (module == nullptr) return nilValue();
  std::string directory =
      directoryOf(std::string(module->path->chars, module->path->length));
  return objValue((Obj*)vm.runtime().internString(directory));
}

// The directories `import` and ffi_open() search when a name is not found
// beside the file asking for it.
Value nativeLibraryPaths(VM& vm, int, Value*) {
  ObjArray* array = vm.runtime().newArray();
  GCRoot arrayRoot(vm.runtime(), (Obj*)array);
  for (const std::string& directory : librarySearchPaths()) {
    array->items.push_back(
        objValue((Obj*)vm.runtime().internString(directory)));
  }
  return objValue((Obj*)array);
}

// ---- dates ----------------------------------------------------------

// Fills a broken down time from a number of seconds, local or UTC.
bool splitTime(double seconds, bool utc, struct tm* out) {
  std::time_t stamp = (std::time_t)seconds;
  if (utc) return ::gmtime_r(&stamp, out) != nullptr;
  return ::localtime_r(&stamp, out) != nullptr;
}

// date() for now, date(seconds) for a moment, date(seconds, true) for the
// same moment in UTC. The month is 1 to 12 and the weekday is 0 for
// Sunday, which is what every other part of the world's tooling uses.
Value nativeDate(VM& vm, int argCount, Value* args) {
  double seconds = (double)std::time(nullptr);
  if (argCount > 0) {
    if (!isNumber(args[0])) {
      return vm.failAs("type", "date() expects a number of seconds, got %s.",
                       valueTypeName(args[0]));
    }
    seconds = asNumber(args[0]);
  }
  bool utc = argCount > 1 && !isFalsey(args[1]);

  struct tm parts;
  if (!splitTime(seconds, utc, &parts)) {
    return vm.fail("date() cannot represent %s.",
                   valueToString(numberValue(seconds)).c_str());
  }

  Runtime& rt = vm.runtime();
  ObjMap* map = rt.newMap();
  GCRoot mapRoot(rt, (Obj*)map);

  struct Field {
    const char* name;
    double value;
  };
  const Field fields[] = {
      {"year", (double)parts.tm_year + 1900},
      {"month", (double)parts.tm_mon + 1},
      {"day", (double)parts.tm_mday},
      {"hour", (double)parts.tm_hour},
      {"minute", (double)parts.tm_min},
      {"second", (double)parts.tm_sec},
      {"weekday", (double)parts.tm_wday},
      {"yearday", (double)parts.tm_yday + 1},
  };
  for (const Field& field : fields) {
    ObjString* key = rt.internString(field.name);
    GCRoot keyRoot(rt, (Obj*)key);
    map->entries.set(objValue((Obj*)key), numberValue(field.value));
  }
  return objValue((Obj*)map);
}

// strftime, so the patterns are the ones already written down
// everywhere: %Y-%m-%d, %H:%M:%S, %a, %B and the rest.
Value nativeFormatTime(VM& vm, int argCount, Value* args) {
  if (!isNumber(args[0])) {
    return vm.failAs("type",
                     "format_time() expects a number of seconds, got %s.",
                     valueTypeName(args[0]));
  }
  if (!isString(args[1])) {
    return vm.failAs("type", "format_time() expects a pattern string, got %s.",
                     valueTypeName(args[1]));
  }
  bool utc = argCount > 2 && !isFalsey(args[2]);

  struct tm parts;
  if (!splitTime(asNumber(args[0]), utc, &parts)) {
    return vm.fail("format_time() cannot represent %s.",
                   valueToString(args[0]).c_str());
  }

  std::string pattern(asString(args[1])->chars, asString(args[1])->length);
  // strftime reports 0 both for "did not fit" and for "produced nothing",
  // so the buffer is grown until the result stops filling it exactly.
  std::string out;
  for (size_t size = 64; size <= 8192; size *= 4) {
    out.assign(size, '\0');
    size_t written = std::strftime(&out[0], size, pattern.c_str(), &parts);
    if (written > 0 || pattern.empty()) {
      out.resize(written);
      return objValue((Obj*)vm.runtime().copyString(out.data(), out.size()));
    }
  }
  return vm.fail("format_time() pattern produced more than 8192 characters.");
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
  defineGlobalFn(runtime, "date", nativeDate, -1);
  defineGlobalFn(runtime, "format_time", nativeFormatTime, -1);
  defineGlobalFn(runtime, "cwd", nativeCwd, 0);
  defineGlobalFn(runtime, "source_path", nativeSourcePath, 0);
  defineGlobalFn(runtime, "source_dir", nativeSourceDir, 0);
  defineGlobalFn(runtime, "library_paths", nativeLibraryPaths, 0);
  defineGlobalFn(runtime, "exists", nativeExists, 1);
  defineGlobalFn(runtime, "platform", nativePlatform, 0);
  defineGlobalFn(runtime, "cpu_count", nativeCpuCount, 0);
  defineGlobalFn(runtime, "gc_info", nativeGCInfo, 0);
  defineGlobalFn(runtime, "collect", nativeCollect, 0);
}

}  // namespace red
