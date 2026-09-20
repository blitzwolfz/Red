// Command line entry point.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>
#include <vector>

#include "compiler.h"
#include "debug.h"
#include "runtime.h"
#include "scanner.h"
#include "stdlib/builtins.h"
#include "util.h"
#include "vm.h"

namespace red {

namespace {

constexpr const char* kVersion = "0.2.0";

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
      "  red <script.red> [args...]   run a program\n"
      "  red run <script.red> [args]  same, stated explicitly\n"
      "  red repl                     start the interactive prompt\n"
      "  red disasm <script.red>      print the compiled bytecode\n"
      "  red legacy <script.red>      run a script on the v1 Java interpreter\n"
      "  red bench [directory]        run the benchmark suite\n"
      "  red version                  print the version\n"
      "\n"
      "Options:\n"
      "  --trace       print every instruction and the stack as it runs\n"
      "  --gc-log      report each collection\n"
      "  --gc-stress   collect before every allocation, for finding bugs\n",
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
  InterpretResult status = vm.interpret(source, module);
  if (status == InterpretResult::RuntimeError) reportRuntimeError(vm);
  vm.detach();

  runtime.joinAllTasks();
  if (status == InterpretResult::CompileError) return kExitCompileError;
  if (status == InterpretResult::RuntimeError) return kExitRuntimeError;
  return 0;
}

int disassembleScript(Runtime& runtime, const std::string& path) {
  std::string source;
  std::string resolved = absolutePath(path);
  if (!readFile(resolved, &source)) {
    std::fprintf(stderr, "Cannot open '%s'.\n", path.c_str());
    return kExitUsage;
  }

  std::lock_guard<std::mutex> guard(runtime.lock);
  ObjModule* module = makeModule(runtime, resolved, "main");
  ObjFunction* function = compile(runtime, source, module);
  if (function == nullptr) return kExitCompileError;
  disassembleChunk(function->chunk, "<script> " + path);
  return 0;
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
  setExecutablePath(argc > 0 ? argv[0] : "red");

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
      if (positional.size() >= 2 || (positional.size() == 1 &&
                                     arg.size() > 4 &&
                                     arg.substr(arg.size() - 4) == ".red")) {
        for (int j = i + 1; j < argc; j++) positional.push_back(argv[j]);
        break;
      }
    }
  }

  {
    std::lock_guard<std::mutex> guard(runtime.lock);
    installBuiltins(runtime);
  }

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
