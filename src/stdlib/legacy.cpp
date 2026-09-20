// Bridge to the original Java interpreter.
//
// The v1 tree-walking interpreter is kept in legacy/ and built into a jar.
// This file runs it as a child process so that old scripts still work
// without the v2 runtime having to reimplement v1 behaviour.
#include <cstdio>
#include <cstdlib>

#include <sys/stat.h>

#include "../util.h"
#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

bool fileExists(const std::string& path) {
  struct stat info;
  return ::stat(path.c_str(), &info) == 0;
}

// Wraps a path for the shell. Single quotes protect everything except a
// single quote itself, which is spliced in separately.
std::string shellQuote(const std::string& text) {
  std::string out = "'";
  for (char c : text) {
    if (c == '\'') {
      out += "'\\''";
    } else {
      out += c;
    }
  }
  out += "'";
  return out;
}

}  // namespace

std::string findLegacyJar() {
  const char* override = std::getenv("RED_LEGACY_JAR");
  if (override != nullptr && fileExists(override)) return override;

  std::string exeDir = directoryOf(absolutePath(executablePath()));
  const char* candidates[] = {
      "/red-legacy.jar",
      "/../red-legacy.jar",
      "/../legacy/red-legacy.jar",
      "/../share/red/red-legacy.jar",
  };
  for (const char* suffix : candidates) {
    std::string candidate = absolutePath(exeDir + suffix);
    if (fileExists(candidate)) return candidate;
  }
  return "";
}

std::string legacyCommand(const std::string& scriptPath, bool captureOutput) {
  std::string jar = findLegacyJar();
  if (jar.empty()) return "";
  std::string command = "java -cp " + shellQuote(jar) + " redlang.Red " +
                        shellQuote(scriptPath);
  if (captureOutput) command += " 2>&1";
  return command;
}

namespace {

Value nativeLegacy(VM& vm, int, Value* args) {
  if (!isString(args[0])) {
    return vm.fail("legacy() expects a script path, got %s.",
                   valueTypeName(args[0]));
  }
  std::string script(asString(args[0])->chars, asString(args[0])->length);
  if (!fileExists(script)) {
    return vm.fail("legacy() cannot find script '%s'.", script.c_str());
  }
  std::string command = legacyCommand(script, false);
  if (command.empty()) {
    return vm.fail(
        "legacy() cannot find red-legacy.jar. Build it with "
        "legacy/build.sh, or set RED_LEGACY_JAR.");
  }

  // The child writes straight to our stdout, so flush first to keep the
  // output in order.
  std::fflush(stdout);
  vm.releaseLock();
  int status = std::system(command.c_str());
  vm.acquireLock();

  if (status == -1) return vm.fail("legacy() could not start a process.");
  return numberValue((double)((status >> 8) & 0xff));
}

Value nativeLegacyOutput(VM& vm, int, Value* args) {
  if (!isString(args[0])) {
    return vm.fail("legacy_output() expects a script path, got %s.",
                   valueTypeName(args[0]));
  }
  std::string script(asString(args[0])->chars, asString(args[0])->length);
  if (!fileExists(script)) {
    return vm.fail("legacy_output() cannot find script '%s'.", script.c_str());
  }
  std::string command = legacyCommand(script, true);
  if (command.empty()) {
    return vm.fail(
        "legacy_output() cannot find red-legacy.jar. Build it with "
        "legacy/build.sh, or set RED_LEGACY_JAR.");
  }

  std::string output;
  vm.releaseLock();
  FILE* pipe = ::popen(command.c_str(), "r");
  if (pipe != nullptr) {
    char buffer[4096];
    size_t read;
    while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
      output.append(buffer, read);
    }
    ::pclose(pipe);
  }
  vm.acquireLock();

  if (pipe == nullptr) return vm.fail("legacy_output() could not start java.");
  return objValue((Obj*)vm.runtime().copyString(output.data(), output.size()));
}

Value nativeLegacyAvailable(VM&, int, Value*) {
  return boolValue(!findLegacyJar().empty());
}

}  // namespace

void installLegacy(Runtime& runtime) {
  defineGlobalFn(runtime, "legacy", nativeLegacy, 1);
  defineGlobalFn(runtime, "legacy_output", nativeLegacyOutput, 1);
  defineGlobalFn(runtime, "legacy_available", nativeLegacyAvailable, 0);
}

}  // namespace red
