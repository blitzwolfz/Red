// Running another program.
//
// run() takes the command as an array, which is passed to the operating
// system as it stands. Nothing is expanded, quoted or split, so a value
// that came from outside the program cannot turn into another command.
// shell() is the one that hands the text to /bin/sh, for when a pipeline
// or a glob is what was wanted.
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

// What one finished process produced.
struct Result {
  int code = 0;
  std::string out;
  std::string err;
  std::string failure;  // empty unless the process could not be started
};

// Reads both pipes until each closes. One read loop over the two, so a
// program that fills its error pipe while nothing is draining its output
// pipe does not deadlock.
void drain(int outFd, int errFd, std::string* out, std::string* err) {
  char buffer[4096];
  while (outFd >= 0 || errFd >= 0) {
    fd_set readable;
    FD_ZERO(&readable);
    int highest = -1;
    if (outFd >= 0) {
      FD_SET(outFd, &readable);
      highest = outFd > highest ? outFd : highest;
    }
    if (errFd >= 0) {
      FD_SET(errFd, &readable);
      highest = errFd > highest ? errFd : highest;
    }
    if (::select(highest + 1, &readable, nullptr, nullptr, nullptr) < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (outFd >= 0 && FD_ISSET(outFd, &readable)) {
      ssize_t got = ::read(outFd, buffer, sizeof(buffer));
      if (got > 0) {
        out->append(buffer, (size_t)got);
      } else {
        ::close(outFd);
        outFd = -1;
      }
    }
    if (errFd >= 0 && FD_ISSET(errFd, &readable)) {
      ssize_t got = ::read(errFd, buffer, sizeof(buffer));
      if (got > 0) {
        err->append(buffer, (size_t)got);
      } else {
        ::close(errFd);
        errFd = -1;
      }
    }
  }
  if (outFd >= 0) ::close(outFd);
  if (errFd >= 0) ::close(errFd);
}

// Starts the program, collects what it wrote, and waits for it. The
// caller must have released the runtime lock: this blocks for as long as
// the other program runs.
Result spawnAndWait(const std::vector<std::string>& argv,
                    const std::string& input) {
  Result result;

  int toChild[2];
  int fromChild[2];
  int fromChildErr[2];
  // A fourth pipe, closed automatically when exec succeeds. If exec
  // fails instead, the child writes its errno here and the parent knows
  // the difference between a program that could not be started and one
  // that ran and exited 127.
  int execReport[2];
  if (::pipe(toChild) != 0) {
    result.failure = std::strerror(errno);
    return result;
  }
  if (::pipe(fromChild) != 0) {
    result.failure = std::strerror(errno);
    ::close(toChild[0]);
    ::close(toChild[1]);
    return result;
  }
  if (::pipe(fromChildErr) != 0) {
    result.failure = std::strerror(errno);
    ::close(toChild[0]);
    ::close(toChild[1]);
    ::close(fromChild[0]);
    ::close(fromChild[1]);
    return result;
  }
  if (::pipe(execReport) != 0) {
    result.failure = std::strerror(errno);
    ::close(toChild[0]);
    ::close(toChild[1]);
    ::close(fromChild[0]);
    ::close(fromChild[1]);
    ::close(fromChildErr[0]);
    ::close(fromChildErr[1]);
    return result;
  }
  ::fcntl(execReport[1], F_SETFD, FD_CLOEXEC);

  // Built before the fork. Between fork and exec only the calls that are
  // safe in a process that had other threads may be used, and allocating
  // is not one of them.
  std::vector<char*> raw;
  raw.reserve(argv.size() + 1);
  for (const std::string& part : argv) raw.push_back((char*)part.c_str());
  raw.push_back(nullptr);

  pid_t child = ::fork();
  if (child < 0) {
    result.failure = std::strerror(errno);
    ::close(toChild[0]);
    ::close(toChild[1]);
    ::close(fromChild[0]);
    ::close(fromChild[1]);
    ::close(fromChildErr[0]);
    ::close(fromChildErr[1]);
    ::close(execReport[0]);
    ::close(execReport[1]);
    return result;
  }

  if (child == 0) {
    ::dup2(toChild[0], 0);
    ::dup2(fromChild[1], 1);
    ::dup2(fromChildErr[1], 2);
    ::close(toChild[0]);
    ::close(toChild[1]);
    ::close(fromChild[0]);
    ::close(fromChild[1]);
    ::close(fromChildErr[0]);
    ::close(fromChildErr[1]);
    ::close(execReport[0]);
    ::execvp(raw[0], raw.data());
    // Only reached when the program could not be started. The report
    // pipe would have closed itself had exec worked, so writing to it
    // says which of the two happened.
    int reason = errno;
    ssize_t ignored = ::write(execReport[1], &reason, sizeof(reason));
    (void)ignored;
    ::_exit(127);
  }

  ::close(toChild[0]);
  ::close(fromChild[1]);
  ::close(fromChildErr[1]);
  ::close(execReport[1]);

  int reason = 0;
  ssize_t reported = ::read(execReport[0], &reason, sizeof(reason));
  ::close(execReport[0]);
  if (reported == (ssize_t)sizeof(reason)) {
    // exec never happened, so there is nothing on the other pipes and
    // nothing ran. Reap the child and report why.
    ::close(toChild[1]);
    ::close(fromChild[0]);
    ::close(fromChildErr[0]);
    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    result.failure = std::strerror(reason);
    return result;
  }

  // Anything the child is meant to read on its input goes in first. A
  // child that exits without reading it would otherwise deliver SIGPIPE
  // here, which is not this program's fault to die of.
  if (!input.empty()) {
    void (*previous)(int) = ::signal(SIGPIPE, SIG_IGN);
    size_t at = 0;
    while (at < input.size()) {
      ssize_t put = ::write(toChild[1], input.data() + at, input.size() - at);
      if (put <= 0) break;
      at += (size_t)put;
    }
    ::signal(SIGPIPE, previous);
  }
  ::close(toChild[1]);

  drain(fromChild[0], fromChildErr[0], &result.out, &result.err);

  int status = 0;
  while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
  }
  if (WIFEXITED(status)) {
    result.code = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    // The convention every shell uses for a program killed by a signal.
    result.code = 128 + WTERMSIG(status);
  }
  return result;
}

// Builds the map a run gives back.
Value resultMap(VM& vm, const Result& result) {
  Runtime& rt = vm.runtime();
  ObjMap* map = rt.newMap();
  GCRoot mapRoot(rt, (Obj*)map);

  ObjString* codeKey = rt.internString("code");
  GCRoot codeRoot(rt, (Obj*)codeKey);
  map->entries.set(objValue((Obj*)codeKey), numberValue((double)result.code));

  ObjString* outKey = rt.internString("out");
  GCRoot outRoot(rt, (Obj*)outKey);
  ObjString* outValue = rt.copyString(result.out.data(), result.out.size());
  GCRoot outValueRoot(rt, (Obj*)outValue);
  map->entries.set(objValue((Obj*)outKey), objValue((Obj*)outValue));

  ObjString* errKey = rt.internString("err");
  GCRoot errRoot(rt, (Obj*)errKey);
  ObjString* errValue = rt.copyString(result.err.data(), result.err.size());
  GCRoot errValueRoot(rt, (Obj*)errValue);
  map->entries.set(objValue((Obj*)errKey), objValue((Obj*)errValue));

  return objValue((Obj*)map);
}

// run(["git", "status"]) or run(["cat"], "some input").
Value nativeRun(VM& vm, int argCount, Value* args) {
  if (!isArray(args[0])) {
    return vm.failAs("type",
                     "run() expects an array of the program and its "
                     "arguments, got %s.",
                     valueTypeName(args[0]));
  }
  ObjArray* parts = asArray(args[0]);
  if (parts->items.empty()) {
    return vm.failAs("process", "run() needs a program to run.");
  }

  std::vector<std::string> argv;
  argv.reserve(parts->items.size());
  for (Value part : parts->items) {
    if (!isString(part)) {
      return vm.failAs("type", "run() expects strings, got %s.",
                       valueTypeName(part));
    }
    argv.push_back(std::string(asString(part)->chars, asString(part)->length));
  }

  std::string input;
  if (argCount > 1 && !isNil(args[1])) {
    if (!isString(args[1])) {
      return vm.failAs("type", "run() expects input as a string, got %s.",
                       valueTypeName(args[1]));
    }
    input.assign(asString(args[1])->chars, asString(args[1])->length);
  }

  // Another program can take as long as it likes, so the heap is left to
  // the other tasks while this one waits.
  vm.releaseLock();
  Result result = spawnAndWait(argv, input);
  vm.acquireLock();

  if (!result.failure.empty()) {
    return vm.failAs("process", "run() could not start '%s': %s",
                     argv[0].c_str(), result.failure.c_str());
  }
  return resultMap(vm, result);
}

// shell("ls *.red | wc -l"). The text goes to /bin/sh, so everything a
// shell does applies, including everything a shell does to a value that
// came from somewhere else. Prefer run() when the parts are not written
// out in the program.
Value nativeShell(VM& vm, int argCount, Value* args) {
  if (!isString(args[0])) {
    return vm.failAs("type", "shell() expects a command string, got %s.",
                     valueTypeName(args[0]));
  }
  std::string command(asString(args[0])->chars, asString(args[0])->length);

  std::string input;
  if (argCount > 1 && !isNil(args[1])) {
    if (!isString(args[1])) {
      return vm.failAs("type", "shell() expects input as a string, got %s.",
                       valueTypeName(args[1]));
    }
    input.assign(asString(args[1])->chars, asString(args[1])->length);
  }

  std::vector<std::string> argv = {"/bin/sh", "-c", command};
  vm.releaseLock();
  Result result = spawnAndWait(argv, input);
  vm.acquireLock();

  if (!result.failure.empty()) {
    return vm.failAs("process", "shell() could not start /bin/sh: %s",
                     result.failure.c_str());
  }
  return resultMap(vm, result);
}

// Where a program is, or nil. The same search the shell does.
Value nativeWhich(VM& vm, int, Value* args) {
  if (!isString(args[0])) {
    return vm.failAs("type", "which() expects a program name, got %s.",
                     valueTypeName(args[0]));
  }
  std::string name(asString(args[0])->chars, asString(args[0])->length);
  if (name.empty()) return nilValue();

  // A name with a slash in it is a path already.
  if (name.find('/') != std::string::npos) {
    if (::access(name.c_str(), X_OK) == 0) {
      return objValue((Obj*)vm.runtime().copyString(name.data(), name.size()));
    }
    return nilValue();
  }

  const char* path = std::getenv("PATH");
  if (path == nullptr) return nilValue();
  std::string entries(path);
  size_t start = 0;
  while (start <= entries.size()) {
    size_t end = entries.find(':', start);
    if (end == std::string::npos) end = entries.size();
    std::string directory = entries.substr(start, end - start);
    if (!directory.empty()) {
      std::string candidate = directory + "/" + name;
      if (::access(candidate.c_str(), X_OK) == 0) {
        return objValue(
            (Obj*)vm.runtime().copyString(candidate.data(), candidate.size()));
      }
    }
    start = end + 1;
  }
  return nilValue();
}

}  // namespace

void installProcess(Runtime& runtime) {
  defineGlobalFn(runtime, "run", nativeRun, -1);
  defineGlobalFn(runtime, "shell", nativeShell, -1);
  defineGlobalFn(runtime, "which", nativeWhich, 1);
}

}  // namespace red
