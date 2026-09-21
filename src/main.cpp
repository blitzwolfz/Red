// Command line entry point.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "bundle.h"
#include "chunk.h"
#include "compiler.h"
#include "debug.h"
#include "debugger.h"
#include "format.h"
#include "runtime.h"
#include "scanner.h"
#include "serialize.h"
#include "stdlib/builtins.h"
#include "util.h"
#include "vm.h"

namespace red {

namespace {

constexpr const char* kVersion = "0.4.0";

// Exit codes follow the same convention as v1: 65 for a program that did
// not compile, 70 for one that failed while running.
constexpr int kExitCompileError = 65;
constexpr int kExitRuntimeError = 70;
constexpr int kExitUsage = 64;

void printUsage() {
  std::printf(
      "Red %s\n"
      "\n"
      "Usage:\n"
      "  red <program> [args...]      run a program, source or compiled\n"
      "  red run <program> [args]     same, stated explicitly\n"
      "  red compile <in.red> [-o f]  compile ahead of time to a .redc file\n"
      "  red build <in.red> [-o name] write a standalone executable\n"
      "  red repl                     start the interactive prompt\n"
      "  red test [directory]         run the tests in a directory\n"
      "  red debug <script.red>       run a program under the debugger\n"
      "  red fmt [-w|--check] [files] format source, or standard input\n"
      "  red disasm <program>         print the compiled bytecode\n"
      "  red legacy <script.red>      run a script on the v1 Java interpreter\n"
      "  red bench [directory]        run the benchmark suite\n"
      "  red version                  print the version\n"
      "\n"
      "Options:\n"
      "  --trace       print every instruction and the stack as it runs\n"
      "  --gc-log      report each collection\n"
      "  --gc-stress   collect before every allocation, for finding bugs\n"
      "\n"
      "Options for `red test`:\n"
      "  --gc-stress        collect before every allocation in each test\n"
      "  --compiled         compile each test first and run the .redc\n"
      "  --compiler <prog>  compile with this Red program, not the built-in\n"
      "                     compiler. selfhost/redc.red is the one to pass\n"
      "  --filter <text>    only tests whose name contains this\n",
      kVersion);
}

ObjModule* makeModule(Runtime& runtime, const std::string& path,
                      const std::string& name) {
  // Each string has to be rooted as soon as it exists. The interner does
  // not keep strings alive, so allocating the second one can collect the
  // first while it is still only held in a local.
  ObjString* nameString = runtime.internString(name);
  GCRoot nameRoot(runtime, (Obj*)nameString);
  ObjString* pathString = runtime.internString(path);
  GCRoot pathRoot(runtime, (Obj*)pathString);

  ObjModule* module = runtime.newModule(nameString, pathString);
  GCRoot moduleRoot(runtime, (Obj*)module);
  module->loaded = true;
  runtime.modules.set(module->path, objValue((Obj*)module));
  runtime.mainModule = module;
  return module;
}

void reportRuntimeError(VM& vm) {
  Value error = vm.lastError;
  if (isError(error)) {
    ObjError* e = asError(error);
    std::fprintf(stderr, "Runtime error: %s\n", e->message->chars);
    if (e->trace->length > 0) std::fprintf(stderr, "%s", e->trace->chars);
  } else {
    std::fprintf(stderr, "Runtime error: %s\n", valueToString(error).c_str());
  }
}

int runScript(Runtime& runtime, const std::string& path) {
  std::string source;
  std::string resolved = absolutePath(path);
  if (!readFile(resolved, &source)) {
    std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
    return kExitUsage;
  }

  VM vm(runtime);
  vm.attach();
  std::string name = path.substr(path.find_last_of('/') + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);

  ObjModule* module = makeModule(runtime, resolved, name);

  InterpretResult status;
  if (looksCompiled(source)) {
    // Already compiled, so the compiler never runs.
    std::string reason;
    ObjFunction* function = readCompiled(runtime, source, module, &reason);
    if (function == nullptr) {
      std::fprintf(stderr, "Cannot load '%s': %s\n", path.c_str(),
                   reason.c_str());
      vm.detach();
      runtime.joinAllTasks();
      return kExitCompileError;
    }
    status = vm.runFunction(function);
  } else {
    status = vm.interpret(source, module);
  }

  if (status == InterpretResult::RuntimeError) reportRuntimeError(vm);
  vm.detach();

  runtime.joinAllTasks();
  if (status == InterpretResult::CompileError) return kExitCompileError;
  if (status == InterpretResult::RuntimeError) return kExitRuntimeError;
  return 0;
}

// `red debug program.red` runs the program with the debugger attached.
// The source is needed, not a .redc: a compiled file carries no local
// names and no way back to the lines.
int debugScript(Runtime& runtime, const std::string& path,
                const std::vector<std::string>& args) {
  std::string source;
  std::string resolved = absolutePath(path);
  if (!readFile(resolved, &source)) {
    std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
    return kExitUsage;
  }
  if (looksCompiled(source)) {
    std::fprintf(stderr,
                 "'%s' is compiled. Debugging needs the source, because a "
                 ".redc carries no names.\n",
                 path.c_str());
    return kExitUsage;
  }

  for (const std::string& arg : args) runtime.scriptArgs.push_back(arg);

  Debugger debugger(path, source);
  runtime.debugger = &debugger;

  VM vm(runtime);
  vm.attach();
  std::string name = path.substr(path.find_last_of('/') + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  ObjModule* module = makeModule(runtime, resolved, name);

  debugger.start();
  InterpretResult status = vm.interpret(source, module);
  if (status == InterpretResult::RuntimeError) {
    Value error = vm.lastError;
    std::string message = isError(error)
                              ? std::string(asError(error)->message->chars)
                              : valueToString(error);
    debugger.onError(vm, message);
    reportRuntimeError(vm);
  }
  vm.detach();
  runtime.debugger = nullptr;
  runtime.joinAllTasks();

  if (status == InterpretResult::CompileError) return kExitCompileError;
  if (status == InterpretResult::RuntimeError) return kExitRuntimeError;
  return 0;
}

// `red fmt` reads files, or standard input when given none.
int formatFiles(const std::vector<std::string>& paths, bool write,
                bool check) {
  if (paths.empty()) {
    std::string source;
    int c;
    while ((c = std::fgetc(stdin)) != EOF) source += (char)c;
    std::string out;
    std::string reason;
    if (!formatSource(source, &out, &reason)) {
      std::fprintf(stderr, "stdin: %s\n", reason.c_str());
      return kExitCompileError;
    }
    std::fwrite(out.data(), 1, out.size(), stdout);
    return 0;
  }

  int changed = 0;
  int failed = 0;
  for (const std::string& path : paths) {
    std::string source;
    if (!readFile(absolutePath(path), &source)) {
      std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
      failed++;
      continue;
    }
    std::string out;
    std::string reason;
    if (!formatSource(source, &out, &reason)) {
      std::fprintf(stderr, "%s: %s\n", path.c_str(), reason.c_str());
      failed++;
      continue;
    }
    if (out == source) continue;
    changed++;

    if (check) {
      std::printf("%s\n", path.c_str());
      continue;
    }
    if (!write) {
      std::fwrite(out.data(), 1, out.size(), stdout);
      continue;
    }
    FILE* handle = std::fopen(path.c_str(), "wb");
    if (handle == nullptr) {
      std::fprintf(stderr, "Cannot write '%s'.\n", path.c_str());
      failed++;
      continue;
    }
    std::fwrite(out.data(), 1, out.size(), handle);
    std::fclose(handle);
    std::printf("%s\n", path.c_str());
  }

  if (failed > 0) return kExitCompileError;
  // --check reports "something would change" the way every other
  // formatter does, so it can gate a build.
  if (check && changed > 0) return 1;
  return 0;
}

int disassembleScript(Runtime& runtime, const std::string& path) {
  std::string source;
  std::string resolved = absolutePath(path);
  if (!readFile(resolved, &source)) {
    std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
    return kExitUsage;
  }
  ObjModule* module = makeModule(runtime, resolved, "main");

  ObjFunction* function;
  if (looksCompiled(source)) {
    std::string reason;
    function = readCompiled(runtime, source, module, &reason);
    if (function == nullptr) {
      std::fprintf(stderr, "Cannot load '%s': %s\n", path.c_str(),
                   reason.c_str());
      return kExitCompileError;
    }
  } else {
    function = compile(runtime, source, module);
    if (function == nullptr) return kExitCompileError;
  }
  disassembleChunk(function->chunk, "<script> " + path);
  return 0;
}

// Runs the program carried inside this executable. A bundled program
// gets every argument it was started with, because there is no command
// to pick out: the executable is the program.
int runBundled(Runtime& runtime, const std::string& executable,
               const std::string& payload) {
  std::string name = executable.substr(executable.find_last_of('/') + 1);

  Bundle bundle;
  std::string reason;
  if (!decodeBundle(payload, &bundle, &reason)) {
    std::fprintf(stderr, "%s: damaged program: %s\n", name.c_str(),
                 reason.c_str());
    return kExitCompileError;
  }
  // Imports inside the program are answered from here rather than from
  // the file system, under the paths the build recorded.
  runtime.bundle = &bundle;

  installBuiltins(runtime);

  VM vm(runtime);
  vm.attach();
  // The module keeps the path it had when it was built, because that is
  // the key its own imports are recorded under.
  ObjModule* module =
      makeModule(runtime, bundle.modules[bundle.entry].path, name);

  ObjFunction* function =
      readCompiled(runtime, bundle.modules[bundle.entry].code, module, &reason);
  if (function == nullptr) {
    std::fprintf(stderr, "%s: damaged program: %s\n", name.c_str(),
                 reason.c_str());
    vm.detach();
    runtime.joinAllTasks();
    return kExitCompileError;
  }

  InterpretResult status = vm.runFunction(function);
  if (status == InterpretResult::RuntimeError) reportRuntimeError(vm);
  vm.detach();
  runtime.joinAllTasks();
  if (status == InterpretResult::RuntimeError) return kExitRuntimeError;
  return 0;
}

// Every module path this function's code imports, in the order the
// imports appear. Nested functions live in the constant pool, so the
// walk has to go through them as well as along the code.
void collectImports(ObjFunction* function, std::vector<std::string>* out) {
  const Chunk& chunk = function->chunk;
  for (size_t offset = 0; offset < chunk.code.size();) {
    if (chunk.code[offset] == OP_IMPORT) {
      uint16_t index = (uint16_t)((chunk.code[offset + 1] << 8) |
                                  chunk.code[offset + 2]);
      Value constant = chunk.constants[index];
      if (isString(constant)) {
        std::string request(asString(constant)->chars,
                            asString(constant)->length);
        if (std::find(out->begin(), out->end(), request) == out->end()) {
          out->push_back(request);
        }
      }
    }
    offset += instructionLength(chunk, offset);
  }

  for (Value constant : chunk.constants) {
    if (isObj(constant) && asObj(constant)->type == ObjType::Function) {
      collectImports(asFunction(constant), out);
    }
  }
}

// Resolves an import the same way the VM does, so that what the build
// puts in the bundle is what running from source would have loaded.
std::string resolveImport(const std::string& from, const std::string& request) {
  std::string resolved = absolutePath(joinPath(directoryOf(from), request));
  if (!fileExists(resolved) && !request.empty() && request.front() != '/') {
    std::string found = findOnLibraryPath(request);
    if (!found.empty()) resolved = found;
  }
  return resolved;
}

std::string moduleNameFor(const std::string& path) {
  std::string name = path.substr(path.find_last_of('/') + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);
  return name;
}

// Compiles one module and records where its imports lead. Returns false
// after reporting why.
bool bundleModule(Runtime& runtime, const std::string& path,
                  BundledModule* out, std::vector<std::string>* queue) {
  std::string source;
  if (!readFile(path, &source)) {
    std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
    return false;
  }

  out->path = path;
  out->name = moduleNameFor(path);

  ObjString* nameString = runtime.internString(out->name);
  GCRoot nameRoot(runtime, (Obj*)nameString);
  ObjString* pathString = runtime.internString(path);
  GCRoot pathRoot(runtime, (Obj*)pathString);
  ObjModule* module = runtime.newModule(nameString, pathString);
  GCRoot moduleRoot(runtime, (Obj*)module);

  ObjFunction* function;
  std::string reason;
  if (looksCompiled(source)) {
    // Already a .redc. It goes in as it stands, but it still has to be
    // read so that its imports can be followed.
    out->code = source;
    function = readCompiled(runtime, source, module, &reason);
    if (function == nullptr) {
      std::fprintf(stderr, "Cannot load '%s': %s\n", path.c_str(),
                   reason.c_str());
      return false;
    }
  } else {
    function = compile(runtime, source, module);
    if (function == nullptr) return false;
    if (!writeCompiled(function, &out->code, &reason)) {
      std::fprintf(stderr, "Cannot compile '%s': %s\n", path.c_str(),
                   reason.c_str());
      return false;
    }
  }
  GCRoot functionRoot(runtime, (Obj*)function);

  std::vector<std::string> requests;
  collectImports(function, &requests);
  for (const std::string& request : requests) {
    std::string target = resolveImport(path, request);
    if (!fileExists(target)) {
      std::fprintf(stderr, "Cannot find module '%s', imported by '%s'.\n",
                   request.c_str(), path.c_str());
      return false;
    }
    out->links.emplace_back(request, target);
    queue->push_back(target);
  }
  return true;
}

// `red build app.red -o app` writes a copy of this interpreter with the
// program, and every module it imports, on the end of it.
int buildExecutable(Runtime& runtime, const std::string& path,
                    const std::string& outPath) {
  std::string resolved = absolutePath(path);
  if (!fileExists(resolved)) {
    std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
    return kExitUsage;
  }

  Bundle bundle;
  std::vector<std::string> queue{resolved};
  std::vector<std::string> seen;
  while (!queue.empty()) {
    std::string next = queue.front();
    queue.erase(queue.begin());
    if (std::find(seen.begin(), seen.end(), next) != seen.end()) continue;
    seen.push_back(next);

    BundledModule module;
    if (!bundleModule(runtime, next, &module, &queue)) return kExitCompileError;
    bundle.modules.push_back(std::move(module));
  }
  // The first module compiled is the one that was asked for, and an
  // import cycle cannot move it, because a path already seen is skipped.
  bundle.entry = 0;

  std::string reason;
  std::string payload = encodeBundle(bundle);
  if (!writeBundle(executablePath(), payload, outPath, &reason)) {
    std::fprintf(stderr, "Cannot build '%s': %s\n", outPath.c_str(),
                 reason.c_str());
    return kExitUsage;
  }

  struct stat info;
  size_t total = ::stat(outPath.c_str(), &info) == 0 ? (size_t)info.st_size : 0;
  std::printf("%s -> %s (%zu bytes, %zu of them program, %zu module%s)\n",
              path.c_str(), outPath.c_str(), total, payload.size(),
              bundle.modules.size(),
              bundle.modules.size() == 1 ? "" : "s");
  return 0;
}

// Drops a .red or .redc suffix, and any directory part. `red build
// src/app.red` writes `app` in the working directory.
std::string executableNameFor(const std::string& path) {
  std::string name = path.substr(path.find_last_of('/') + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot != 0) name = name.substr(0, dot);
  if (name.empty()) name = "a.out";
  return name;
}

// Replaces a .red suffix with .redc, or adds it.
std::string compiledNameFor(const std::string& path) {
  if (path.size() > 4 && path.compare(path.size() - 4, 4, ".red") == 0) {
    return path + "c";
  }
  return path + ".redc";
}

int compileToFile(Runtime& runtime, const std::string& path,
                  const std::string& outPath) {
  std::string source;
  std::string resolved = absolutePath(path);
  if (!readFile(resolved, &source)) {
    std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
    return kExitUsage;
  }
  if (looksCompiled(source)) {
    std::fprintf(stderr, "'%s' is already compiled.\n", path.c_str());
    return kExitUsage;
  }
  std::string name = path.substr(path.find_last_of('/') + 1);
  size_t dot = name.find_last_of('.');
  if (dot != std::string::npos) name = name.substr(0, dot);

  ObjModule* module = makeModule(runtime, resolved, name);
  ObjFunction* function = compile(runtime, source, module);
  if (function == nullptr) return kExitCompileError;
  GCRoot functionRoot(runtime, (Obj*)function);

  std::string bytes;
  std::string reason;
  if (!writeCompiled(function, &bytes, &reason)) {
    std::fprintf(stderr, "Cannot compile '%s': %s\n", path.c_str(),
                 reason.c_str());
    return kExitCompileError;
  }

  FILE* out = std::fopen(outPath.c_str(), "wb");
  if (out == nullptr) {
    std::fprintf(stderr, "Cannot write '%s'.\n", outPath.c_str());
    return kExitUsage;
  }
  size_t written = std::fwrite(bytes.data(), 1, bytes.size(), out);
  std::fclose(out);
  if (written != bytes.size()) {
    std::fprintf(stderr, "Cannot write '%s'.\n", outPath.c_str());
    return kExitUsage;
  }

  std::printf("%s -> %s (%zu bytes)\n", path.c_str(), outPath.c_str(),
              bytes.size());
  return 0;
}


// ---------------------------------------------------------------------
// The test runner.
//
// A test is a Red program with its expected output written in it as
// comments, so that it reads on its own:
//
//   print(1 + 1);                  // expect: 2
//   // expect runtime error: Division by zero.
//   // expect compile error: Expect ';'
//
// Lines marked `expect` must appear on standard output in that order. An
// expected error is matched as a substring of standard error, and brings
// the exit code with it: 70 for a runtime error, 65 for one at compile
// time.

struct Expectations {
  std::vector<std::string> output;
  std::vector<std::string> runtimeErrors;
  std::vector<std::string> compileErrors;
};

// Text after a `// expect...:` marker, or nothing. At most one space
// after the colon belongs to the marker; the rest is expected output, so
// a test can expect a line that begins with a space.
bool expectationOn(const std::string& line, const char* keyword,
                   std::string* out) {
  size_t keywordLength = std::strlen(keyword);
  size_t at = 0;
  while ((at = line.find("//", at)) != std::string::npos) {
    size_t cursor = at + 2;
    while (cursor < line.size() && (line[cursor] == ' ' || line[cursor] == '\t')) {
      cursor++;
    }
    if (line.compare(cursor, keywordLength, keyword) == 0) {
      cursor += keywordLength;
      if (cursor < line.size() && line[cursor] == ' ') cursor++;
      *out = line.substr(cursor);
      return true;
    }
    at += 2;
  }
  return false;
}

Expectations readExpectations(const std::string& source) {
  Expectations wanted;
  size_t start = 0;
  while (start <= source.size()) {
    size_t end = source.find('\n', start);
    if (end == std::string::npos) end = source.size();
    std::string line = source.substr(start, end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::string text;
    if (expectationOn(line, "expect runtime error:", &text)) {
      wanted.runtimeErrors.push_back(text);
    } else if (expectationOn(line, "expect compile error:", &text)) {
      wanted.compileErrors.push_back(text);
    } else if (expectationOn(line, "expect:", &text)) {
      wanted.output.push_back(text);
    }
    start = end + 1;
  }
  return wanted;
}

std::string quoteForShell(const std::string& text) {
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

// Runs one command with its streams captured. Returns the exit status.
int runCaptured(const std::string& command, const std::string& directory,
                const std::string& outPath, const std::string& errPath,
                std::string* out, std::string* err) {
  std::string full = "cd " + quoteForShell(directory) + " && " + command +
                     " > " + quoteForShell(outPath) + " 2> " +
                     quoteForShell(errPath);
  int status = std::system(full.c_str());
  readFile(outPath, out);
  readFile(errPath, err);
  if (status == -1) return -1;
  return (status >> 8) & 0xff;
}

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    lines.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return lines;
}

struct TestOptions {
  bool gcStress = false;
  bool compiled = false;
  // A Red program to compile with instead of the built-in compiler,
  // which is how the self-hosted one is put under the whole suite.
  std::string compiler;
  std::string filter;
};

// Collects every .red file under `directory`, sorted. Files in a folder
// named `modules` are imported by other tests rather than run on their
// own, so they are left out.
void collectTests(const std::string& directory, std::vector<std::string>* out) {
  DIR* handle = ::opendir(directory.c_str());
  if (handle == nullptr) return;

  std::vector<std::string> names;
  for (;;) {
    struct dirent* entry = ::readdir(handle);
    if (entry == nullptr) break;
    std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    names.push_back(name);
  }
  ::closedir(handle);
  std::sort(names.begin(), names.end());

  for (const std::string& name : names) {
    std::string path = joinPath(directory, name);
    struct stat info;
    if (::stat(path.c_str(), &info) != 0) continue;
    if (S_ISDIR(info.st_mode)) {
      if (name == "modules") continue;
      collectTests(path, out);
    } else if (name.size() > 4 &&
               name.compare(name.size() - 4, 4, ".red") == 0) {
      out->push_back(path);
    }
  }
}

int runTests(const std::string& directory, const TestOptions& options) {
  std::vector<std::string> paths;
  collectTests(directory, &paths);
  if (paths.empty()) {
    std::fprintf(stderr, "No .red files under '%s'.\n", directory.c_str());
    return kExitUsage;
  }

  // Colour only when someone is watching. Piped into a file or a log it
  // would just be noise.
  bool colour = ::isatty(1) != 0;
  const char* green = colour ? "\033[32m" : "";
  const char* red = colour ? "\033[31m" : "";
  const char* dim = colour ? "\033[2m" : "";
  const char* off = colour ? "\033[0m" : "";

  char temporary[] = "/tmp/red-test-XXXXXX";
  if (::mkdtemp(temporary) == nullptr) {
    std::fprintf(stderr, "Cannot make a temporary directory.\n");
    return kExitUsage;
  }
  std::string outPath = std::string(temporary) + "/out";
  std::string errPath = std::string(temporary) + "/err";

  std::string self = quoteForShell(absolutePath(executablePath()));
  std::string base = absolutePath(directory);
  int passed = 0;
  int failed = 0;

  for (const std::string& path : paths) {
    std::string name = absolutePath(path);
    if (name.compare(0, base.size(), base) == 0 && name.size() > base.size()) {
      name = name.substr(base.size() + 1);
    }
    if (!options.filter.empty() &&
        name.find(options.filter) == std::string::npos) {
      continue;
    }

    std::string source;
    if (!readFile(absolutePath(path), &source)) continue;
    Expectations wanted = readExpectations(source);

    std::string absolute = absolutePath(path);
    std::string directoryOfTest = directoryOf(absolute);
    std::vector<std::string> problems;
    std::string toRun = absolute;
    std::string compiledPath;

    // A test that is meant not to compile has nothing to run compiled.
    bool skipCompile = wanted.compileErrors.empty() == false;
    if (options.compiled && !skipCompile) {
      compiledPath = absolute.substr(0, absolute.size() - 4) + ".redc";
      std::string command = self;
      if (!options.compiler.empty()) {
        command += " " + quoteForShell(absolutePath(options.compiler));
      }
      command += " compile " + quoteForShell(absolute) + " -o " +
                 quoteForShell(compiledPath);
      std::string ignored, reason;
      int status = runCaptured(command, directoryOfTest, outPath, errPath,
                               &ignored, &reason);
      if (status != 0) {
        problems.push_back("could not compile ahead of time: " + reason);
      } else {
        toRun = compiledPath;
      }
    }

    std::string out;
    std::string err;
    int status = 0;
    if (problems.empty()) {
      std::string command = self;
      if (options.gcStress) command += " --gc-stress";
      command += " " + quoteForShell(toRun);
      status = runCaptured(command, directoryOfTest, outPath, errPath, &out,
                           &err);
    }
    if (!compiledPath.empty()) ::remove(compiledPath.c_str());

    if (problems.empty()) {
      std::vector<std::string> actual = splitLines(out);
      for (size_t i = 0; i < wanted.output.size(); i++) {
        if (i >= actual.size()) {
          problems.push_back("line " + std::to_string(i + 1) + ": expected '" +
                             wanted.output[i] + "', got nothing");
        } else if (actual[i] != wanted.output[i]) {
          problems.push_back("line " + std::to_string(i + 1) + ": expected '" +
                             wanted.output[i] + "', got '" + actual[i] + "'");
        }
      }
      for (size_t i = wanted.output.size(); i < actual.size(); i++) {
        problems.push_back("unexpected output '" + actual[i] + "'");
      }

      for (const std::string& want : wanted.runtimeErrors) {
        if (err.find(want) == std::string::npos) {
          problems.push_back("expected a runtime error containing '" + want +
                             "'");
        }
      }
      for (const std::string& want : wanted.compileErrors) {
        if (err.find(want) == std::string::npos) {
          problems.push_back("expected a compile error containing '" + want +
                             "'");
        }
      }

      int expectedStatus = 0;
      if (!wanted.runtimeErrors.empty()) {
        expectedStatus = kExitRuntimeError;
      } else if (!wanted.compileErrors.empty()) {
        expectedStatus = kExitCompileError;
      }
      if (status != expectedStatus) {
        std::string note = "expected exit code " +
                           std::to_string(expectedStatus) + ", got " +
                           std::to_string(status);
        if (expectedStatus == 0 && !err.empty()) note += "\n     " + err;
        problems.push_back(note);
      }
    }

    if (problems.empty()) {
      passed++;
      std::printf("%sok%s   %s\n", green, off, name.c_str());
    } else {
      failed++;
      std::printf("%sFAIL%s %s\n", red, off, name.c_str());
      for (const std::string& problem : problems) {
        std::printf("     %s%s%s\n", dim, problem.c_str(), off);
      }
    }
  }

  ::remove(outPath.c_str());
  ::remove(errPath.c_str());
  ::rmdir(temporary);

  std::string modes;
  if (options.gcStress) modes = "gc stress";
  if (!options.compiler.empty()) {
    modes += modes.empty() ? "" : ", ";
    modes += "compiled by " + options.compiler;
  } else if (options.compiled) {
    modes += modes.empty() ? "" : ", ";
    modes += "compiled ahead of time";
  }
  std::printf("\n%d/%d tests passed%s%s%s\n", passed, passed + failed,
              modes.empty() ? "" : " (", modes.c_str(),
              modes.empty() ? "" : ")");
  return failed == 0 ? 0 : 1;
}

// Returns true when the text so far cannot be a complete program, so the
// prompt should keep reading. Using the real scanner means braces inside
// strings and comments do not confuse it.
bool needsMoreInput(const std::string& source) {
  Scanner scanner(source);
  int depth = 0;
  for (;;) {
    Token token = scanner.scan();
    if (token.type == TokenType::Eof) break;
    if (token.type == TokenType::Error) {
      // An unterminated string is the usual reason a pasted line is not
      // finished yet.
      return token.lexeme.find("Unterminated") != std::string::npos;
    }
    switch (token.type) {
      case TokenType::LeftBrace:
      case TokenType::LeftParen:
      case TokenType::LeftBracket:
        depth++;
        break;
      case TokenType::RightBrace:
      case TokenType::RightParen:
      case TokenType::RightBracket:
        depth--;
        break;
      default:
        break;
    }
  }
  return depth > 0;
}

int runRepl(Runtime& runtime) {
  std::printf("Red %s. Type a statement, or Ctrl-D to leave.\n", kVersion);

  VM vm(runtime);
  vm.attach();
  ObjModule* module = makeModule(runtime, absolutePath("repl"), "repl");

  std::string pending;
  for (;;) {
    std::printf("%s", pending.empty() ? "red> " : "...> ");
    std::fflush(stdout);

    std::string line;
    int c;
    bool gotAny = false;
    while ((c = std::fgetc(stdin)) != EOF) {
      gotAny = true;
      if (c == '\n') break;
      line += (char)c;
    }
    if (!gotAny) {
      std::printf("\n");
      break;
    }

    pending += line;
    pending += "\n";
    if (needsMoreInput(pending)) continue;

    std::string source = pending;
    pending.clear();

    // A bare expression is more useful printed than discarded, so it is
    // tried as an argument to print first. The attempt is quiet, and the
    // input is compiled normally if it was not an expression.
    std::string trimmed = source;
    while (!trimmed.empty() && std::isspace((unsigned char)trimmed.back())) {
      trimmed.pop_back();
    }
    if (trimmed.empty()) continue;

    ObjFunction* function = nullptr;
    if (trimmed.back() != ';' && trimmed.back() != '}') {
      function = compile(runtime, "print(" + trimmed + ");", module, true);
    }
    if (function == nullptr) {
      function = compile(runtime, source, module, false);
    }
    if (function == nullptr) continue;

    if (vm.runFunction(function) == InterpretResult::RuntimeError) {
      reportRuntimeError(vm);
    }
  }

  vm.detach();
  runtime.joinAllTasks();
  return 0;
}

int runLegacy(const std::string& path) {
  std::string command = legacyCommand(absolutePath(path), false);
  if (command.empty()) {
    std::fprintf(stderr,
                 "Cannot find red-legacy.jar. Build it with legacy/build.sh, "
                 "or set RED_LEGACY_JAR.\n");
    return kExitUsage;
  }
  int status = std::system(command.c_str());
  if (status == -1) {
    std::fprintf(stderr, "Could not start java.\n");
    return kExitRuntimeError;
  }
  return (status >> 8) & 0xff;
}

int runBench(Runtime& runtime, const std::string& directory) {
  const char* names[] = {"fib.red", "loop.red", "string.red", "alloc.red",
                         "method.red"};

  // Each program prints its own result, which is how the comparison
  // script checks that both versions did the same work. Timings are
  // collected first and the table is printed at the end, so that output
  // does not land in the middle of it.
  std::vector<std::pair<std::string, double>> results;
  int failures = 0;

  for (const char* name : names) {
    std::string path = joinPath(directory, name);
    std::string source;
    if (!readFile(path, &source)) {
      std::fprintf(stderr, "skipping %s: not found\n", name);
      continue;
    }

    std::printf("running %s ... ", name);
    std::fflush(stdout);

    VM vm(runtime);
    vm.attach();
    ObjModule* module = makeModule(runtime, absolutePath(path), name);
    double start = (double)std::clock() / CLOCKS_PER_SEC;
    InterpretResult status = vm.interpret(source, module);
    double elapsed = (double)std::clock() / CLOCKS_PER_SEC - start;
    if (status != InterpretResult::Ok) {
      if (status == InterpretResult::RuntimeError) reportRuntimeError(vm);
      failures++;
    }
    vm.detach();
    results.push_back({name, elapsed});
  }
  runtime.joinAllTasks();

  std::printf("\n%-14s %10s\n", "benchmark", "seconds");
  std::printf("%-14s %10s\n", "-------------", "---------");
  for (const auto& result : results) {
    std::printf("%-14s %10.3f\n", result.first.c_str(), result.second);
  }
  std::printf(
      "\nProcessor time, one run each. For a comparison against another\n"
      "interpreter use: python3 bench/compare.py --red build/red\n");
  return failures == 0 ? 0 : kExitRuntimeError;
}

}  // namespace

int main(int argc, const char* argv[]) {
  Runtime runtime;
  std::string self =
      selfExecutablePath(argc > 0 ? argv[0] : "red");
  setExecutablePath(self);

  // A bundled program comes first: the executable is that program, and
  // none of the interpreter's own commands apply to it.
  std::string bundled;
  if (readBundle(self, &bundled)) {
    for (int i = 1; i < argc; i++) runtime.scriptArgs.push_back(argv[i]);
    return runBundled(runtime, self, bundled);
  }

  // `red test` takes flags of its own, so everything after it is handed
  // over untouched rather than read as options for the interpreter. Only
  // the first thing that is not a global flag can be the command: a
  // program is free to have an argument called "test".
  int first = argc;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--trace" || arg == "--gc-log" || arg == "--gc-stress") continue;
    first = i;
    break;
  }
  if (first < argc && std::string(argv[first]) == "test") {
    int i = first;
    TestOptions options;
    std::string directory = "tests";
    bool sawDirectory = false;
    for (int j = i + 1; j < argc; j++) {
      std::string arg = argv[j];
      if (arg == "--gc-stress") {
        options.gcStress = true;
      } else if (arg == "--compiled") {
        options.compiled = true;
      } else if (arg == "--compiler" && j + 1 < argc) {
        options.compiler = argv[++j];
        options.compiled = true;
      } else if (arg == "--filter" && j + 1 < argc) {
        options.filter = argv[++j];
      } else if (arg.rfind("--", 0) == 0) {
        std::fprintf(stderr, "Unknown option '%s' for `red test`.\n",
                     arg.c_str());
        return kExitUsage;
      } else if (!sawDirectory) {
        directory = arg;
        sawDirectory = true;
      } else {
        std::fprintf(stderr, "`red test` takes one directory.\n");
        return kExitUsage;
      }
    }
    installBuiltins(runtime);
    return runTests(directory, options);
  }

  std::vector<std::string> positional;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    if (arg == "--trace") {
      runtime.traceExecution = true;
    } else if (arg == "--gc-log") {
      runtime.logGC = true;
    } else if (arg == "--gc-stress") {
      runtime.stressGC = true;
    } else if (arg == "--help" || arg == "-h") {
      printUsage();
      return 0;
    } else {
      positional.push_back(arg);
      // Everything after the script name belongs to the script.
      bool looksLikeAProgram =
          (arg.size() > 4 && arg.compare(arg.size() - 4, 4, ".red") == 0) ||
          (arg.size() > 5 && arg.compare(arg.size() - 5, 5, ".redc") == 0);
      if (positional.size() >= 2 ||
          (positional.size() == 1 && looksLikeAProgram)) {
        for (int j = i + 1; j < argc; j++) positional.push_back(argv[j]);
        break;
      }
    }
  }

  installBuiltins(runtime);

  if (positional.empty()) return runRepl(runtime);

  const std::string& command = positional[0];
  if (command == "version") {
    std::printf("red %s\n", kVersion);
    return 0;
  }
  if (command == "help") {
    printUsage();
    return 0;
  }
  if (command == "repl") return runRepl(runtime);
  if (command == "fmt") {
    bool write = false;
    bool check = false;
    std::vector<std::string> files;
    for (size_t i = 1; i < positional.size(); i++) {
      if (positional[i] == "-w") {
        write = true;
      } else if (positional[i] == "--check") {
        check = true;
      } else {
        files.push_back(positional[i]);
      }
    }
    return formatFiles(files, write, check);
  }
  if (command == "debug") {
    if (positional.size() < 2) {
      std::fprintf(stderr, "Usage: red debug <script.red> [args...]\n");
      return kExitUsage;
    }
    std::vector<std::string> rest(positional.begin() + 2, positional.end());
    return debugScript(runtime, positional[1], rest);
  }
  if (command == "disasm") {
    if (positional.size() < 2) {
      std::fprintf(stderr, "Usage: red disasm <script.red>\n");
      return kExitUsage;
    }
    return disassembleScript(runtime, positional[1]);
  }
  if (command == "legacy") {
    if (positional.size() < 2) {
      std::fprintf(stderr, "Usage: red legacy <script.red>\n");
      return kExitUsage;
    }
    return runLegacy(positional[1]);
  }
  if (command == "build") {
    if (positional.size() < 2) {
      std::fprintf(stderr, "Usage: red build <script.red> [-o name]\n");
      return kExitUsage;
    }
    std::string outPath = executableNameFor(positional[1]);
    for (size_t i = 2; i + 1 < positional.size(); i++) {
      if (positional[i] == "-o") outPath = positional[i + 1];
    }
    return buildExecutable(runtime, positional[1], outPath);
  }
  if (command == "compile") {
    if (positional.size() < 2) {
      std::fprintf(stderr, "Usage: red compile <script.red> [-o out.redc]\n");
      return kExitUsage;
    }
    std::string outPath = compiledNameFor(positional[1]);
    for (size_t i = 2; i + 1 < positional.size(); i++) {
      if (positional[i] == "-o") outPath = positional[i + 1];
    }
    return compileToFile(runtime, positional[1], outPath);
  }
  if (command == "bench") {
    std::string directory = positional.size() > 1 ? positional[1] : "bench";
    return runBench(runtime, directory);
  }

  size_t scriptIndex = 0;
  if (command == "run") {
    if (positional.size() < 2) {
      std::fprintf(stderr, "Usage: red run <script.red> [args...]\n");
      return kExitUsage;
    }
    scriptIndex = 1;
  }

  for (size_t i = scriptIndex + 1; i < positional.size(); i++) {
    runtime.scriptArgs.push_back(positional[i]);
  }
  return runScript(runtime, positional[scriptIndex]);
}

}  // namespace red

int main(int argc, const char* argv[]) { return red::main(argc, argv); }
