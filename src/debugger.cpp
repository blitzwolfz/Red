#include "debugger.h"

#include <cstdio>
#include <cstring>

#include "vm.h"

namespace red {

namespace {

std::vector<std::string> splitLines(const std::string& text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find('\n', start);
    if (end == std::string::npos) end = text.size();
    out.push_back(text.substr(start, end - start));
    if (end == text.size()) break;
    start = end + 1;
  }
  return out;
}

std::string trim(const std::string& text) {
  size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  size_t last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

// Splits a command into its verb and the rest of the line.
void splitCommand(const std::string& line, std::string* verb,
                  std::string* rest) {
  std::string text = trim(line);
  size_t space = text.find(' ');
  if (space == std::string::npos) {
    *verb = text;
    rest->clear();
    return;
  }
  *verb = text.substr(0, space);
  *rest = trim(text.substr(space + 1));
}

const char* kHelp =
    "  s, step        run to the next line, into calls\n"
    "  n, next        run to the next line, over calls\n"
    "  f, finish      run until this function returns\n"
    "  c, continue    run until a breakpoint\n"
    "  b <line>       set a breakpoint\n"
    "  b              list breakpoints\n"
    "  d <line>       delete a breakpoint\n"
    "  l, list        show the lines around here\n"
    "  bt             show the call stack\n"
    "  v, vars        show the locals in this frame\n"
    "  p <name>       show one variable\n"
    "  q, quit        stop the program\n"
    "  h, help        this\n";

}  // namespace

Debugger::Debugger(const std::string& path, const std::string& source)
    : path_(path), lines_(splitLines(source)) {}

void Debugger::start() {
  std::printf("Red debugger. `h` for the commands, `s` to step.\n");
}

void Debugger::listAround(int line, int radius) {
  int first = line - radius < 1 ? 1 : line - radius;
  int last = line + radius;
  if (last > (int)lines_.size()) last = (int)lines_.size();
  for (int i = first; i <= last; i++) {
    std::printf("%s %4d  %s\n", i == line ? "->" : "  ", i,
                lines_[(size_t)i - 1].c_str());
  }
}

void Debugger::showLocation(VM& vm, CallFrame* frame, int line) {
  ObjFunction* function = frame->closure->function;
  const char* name =
      function->name == nullptr ? "<script>" : function->name->chars;
  std::printf("\n%s:%d  in %s\n", path_.c_str(), line, name);
  if (line >= 1 && line <= (int)lines_.size()) {
    std::printf("  %s\n", trim(lines_[(size_t)line - 1]).c_str());
  }
  (void)vm;
}

void Debugger::showBacktrace(VM& vm) {
  std::string trace = vm.buildTrace();
  if (trace.empty()) {
    std::printf("  <no frames>\n");
    return;
  }
  std::printf("%s", trace.c_str());
}

// Finds the name a slot holds at this point in the code. Slots are
// reused between scopes, so the answer depends on where execution is.
bool Debugger::lookUp(VM& vm, CallFrame* frame, const std::string& name,
                      Value* out) {
  ObjFunction* function = frame->closure->function;
  size_t offset = (size_t)(frame->ip - function->chunk.code.data());

  // Later entries win, so an inner scope shadows an outer one.
  bool found = false;
  for (const LocalName& entry : function->localNames) {
    if (entry.name != name) continue;
    if ((int)offset < entry.start || (int)offset > entry.end) continue;
    *out = frame->slots[entry.slot];
    found = true;
  }
  if (found) return true;

  // Then the module, then the builtins, which is the order the compiler
  // resolves a name it did not find locally.
  ObjString* interned = vm.runtime().internString(name);
  if (vm.currentModule()->globals.get(interned, out)) return true;
  return vm.runtime().builtins.get(interned, out);
}

void Debugger::showLocals(VM& vm, CallFrame* frame, const std::string& only) {
  ObjFunction* function = frame->closure->function;
  size_t offset = (size_t)(frame->ip - function->chunk.code.data());

  if (function->localNames.empty()) {
    std::printf("  no names here: this function came from a .redc\n");
    return;
  }

  bool any = false;
  for (const LocalName& entry : function->localNames) {
    if ((int)offset < entry.start || (int)offset > entry.end) continue;
    // Hidden slots carry a space in their name so that no program can
    // reach them. They are not worth showing either.
    if (entry.name.empty() || entry.name.find(' ') != std::string::npos) {
      continue;
    }
    if (!only.empty() && entry.name != only) continue;
    std::printf("  %-16s %s\n", entry.name.c_str(),
                vm.display(frame->slots[entry.slot]).c_str());
    any = true;
  }
  if (!any) {
    // At the top level everything is a module global rather than a
    // local, so there is genuinely nothing here to list.
    std::printf("  no locals here. `p <name>` reaches globals too.\n");
  }
}

void Debugger::prompt(VM& vm, CallFrame* frame, int line) {
  showLocation(vm, frame, line);

  for (;;) {
    std::printf("(red) ");
    std::fflush(stdout);

    std::string input;
    int c;
    bool got = false;
    while ((c = std::fgetc(stdin)) != EOF) {
      got = true;
      if (c == '\n') break;
      input += (char)c;
    }
    if (!got) {
      // End of input: let the program finish rather than hanging.
      std::printf("\n");
      detached_ = true;
      mode_ = Mode::Run;
      return;
    }

    std::string verb;
    std::string rest;
    splitCommand(input, &verb, &rest);
    if (verb.empty()) verb = "s";

    if (verb == "s" || verb == "step") {
      mode_ = Mode::StepLine;
      return;
    }
    if (verb == "n" || verb == "next") {
      mode_ = Mode::StepOver;
      stepDepth_ = vm.frameDepth();
      return;
    }
    if (verb == "f" || verb == "finish") {
      mode_ = Mode::StepOut;
      stepDepth_ = vm.frameDepth();
      return;
    }
    if (verb == "c" || verb == "continue") {
      mode_ = Mode::Run;
      return;
    }
    if (verb == "q" || verb == "quit") {
      std::printf("Stopped.\n");
      std::exit(0);
    }
    if (verb == "h" || verb == "help") {
      std::printf("%s", kHelp);
      continue;
    }
    if (verb == "l" || verb == "list") {
      listAround(line, 5);
      continue;
    }
    if (verb == "bt") {
      showBacktrace(vm);
      continue;
    }
    if (verb == "v" || verb == "vars") {
      showLocals(vm, frame, "");
      continue;
    }
    if (verb == "p") {
      if (rest.empty()) {
        std::printf("  p needs a name\n");
        continue;
      }
      Value value;
      if (lookUp(vm, frame, rest, &value)) {
        std::printf("  %s = %s\n", rest.c_str(), vm.display(value).c_str());
      } else {
        std::printf("  '%s' is not in scope here\n", rest.c_str());
      }
      continue;
    }
    if (verb == "b") {
      if (rest.empty()) {
        if (breakpoints_.empty()) {
          std::printf("  no breakpoints\n");
        } else {
          for (int at : breakpoints_) std::printf("  line %d\n", at);
        }
        continue;
      }
      int at = std::atoi(rest.c_str());
      if (at <= 0) {
        std::printf("  b takes a line number\n");
        continue;
      }
      breakpoints_.insert(at);
      std::printf("  breakpoint at line %d\n", at);
      continue;
    }
    if (verb == "d") {
      int at = std::atoi(rest.c_str());
      if (breakpoints_.erase(at) > 0) {
        std::printf("  deleted the breakpoint at line %d\n", at);
      } else {
        std::printf("  no breakpoint at line %d\n", at);
      }
      continue;
    }
    std::printf("  no such command. `h` for the list.\n");
  }
}

void Debugger::beforeInstruction(VM& vm, CallFrame* frame) {
  if (detached_) return;

  ObjFunction* function = frame->closure->function;
  size_t offset = (size_t)(frame->ip - function->chunk.code.data());
  int line = function->chunk.lineAt(offset);
  int depth = vm.frameDepth();

  // Only the first instruction of a line is a place to stop, and only
  // when this is a different line from the one just left. Returning into
  // the middle of a line counts as arriving there again, which is why
  // the depth is part of the comparison.
  if (line == lastLine_ && depth == lastDepth_) return;
  lastLine_ = line;
  lastDepth_ = depth;

  bool stop = false;
  switch (mode_) {
    case Mode::Run:
      stop = breakpoints_.count(line) > 0;
      break;
    case Mode::StepLine:
      stop = true;
      break;
    case Mode::StepOver:
      stop = depth <= stepDepth_ || breakpoints_.count(line) > 0;
      break;
    case Mode::StepOut:
      stop = depth < stepDepth_ || breakpoints_.count(line) > 0;
      break;
  }
  if (!stop) return;

  prompt(vm, frame, line);
}

void Debugger::onError(VM& vm, const std::string& message) {
  if (detached_) return;
  std::printf("\nStopped: %s\n", message.c_str());
  CallFrame* frame = vm.topFrame();
  if (frame == nullptr) return;
  ObjFunction* function = frame->closure->function;
  size_t offset = (size_t)(frame->ip - function->chunk.code.data());
  prompt(vm, frame, function->chunk.lineAt(offset));
}

}  // namespace red
