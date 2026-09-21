// File input and output, and the directories they live in.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../util.h"
#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

ObjString* stringArg(VM& vm, Value value, const char* who) {
  if (!isString(value)) {
    vm.failAs("type", "%s expects a string, got %s.", who, valueTypeName(value));
    return nullptr;
  }
  return asString(value);
}

Value nativeReadFile(VM& vm, int, Value* args) {
  ObjString* path = stringArg(vm, args[0], "read_file()");
  if (path == nullptr) return nilValue();
  std::string contents;
  if (!readFile(std::string(path->chars, path->length), &contents)) {
    return nilValue();
  }
  return objValue(
      (Obj*)vm.runtime().copyString(contents.data(), contents.size()));
}

Value writeFileWithMode(VM& vm, Value* args, const char* mode) {
  ObjString* path = stringArg(vm, args[0], "write_file()");
  if (path == nullptr) return nilValue();
  std::string text = valueToString(args[1]);
  FILE* file = std::fopen(std::string(path->chars, path->length).c_str(), mode);
  if (file == nullptr) return boolValue(false);
  size_t written = std::fwrite(text.data(), 1, text.size(), file);
  std::fclose(file);
  return boolValue(written == text.size());
}

Value nativeWriteFile(VM& vm, int, Value* args) {
  return writeFileWithMode(vm, args, "wb");
}

Value nativeAppendFile(VM& vm, int, Value* args) {
  return writeFileWithMode(vm, args, "ab");
}

Value nativeOpen(VM& vm, int argCount, Value* args) {
  ObjString* path = stringArg(vm, args[0], "open()");
  if (path == nullptr) return nilValue();
  std::string mode = "r";
  if (argCount > 1) {
    ObjString* modeString = stringArg(vm, args[1], "open()");
    if (modeString == nullptr) return nilValue();
    mode = std::string(modeString->chars, modeString->length);
  }
  FILE* handle =
      std::fopen(std::string(path->chars, path->length).c_str(), mode.c_str());
  if (handle == nullptr) {
    return vm.failAs("io", "Cannot open '%s' in mode '%s'.", path->chars, mode.c_str());
  }
  return objValue((Obj*)vm.runtime().newFile(handle, path));
}

Value nativeRemoveFile(VM& vm, int, Value* args) {
  ObjString* path = stringArg(vm, args[0], "remove_file()");
  if (path == nullptr) return nilValue();
  return boolValue(std::remove(std::string(path->chars, path->length).c_str()) ==
                   0);
}

bool requireOpen(VM& vm, ObjFile* file, const char* who) {
  if (!file->open || file->handle == nullptr) {
    vm.failAs("io", "%s on a closed file.", who);
    return false;
  }
  return true;
}

Value fileRead(VM& vm, int, Value* args) {
  ObjFile* file = asFile(args[0]);
  if (!requireOpen(vm, file, "read()")) return nilValue();

  std::string contents;
  char buffer[4096];
  size_t read;
  while ((read = std::fread(buffer, 1, sizeof(buffer), file->handle)) > 0) {
    contents.append(buffer, read);
  }
  return objValue(
      (Obj*)vm.runtime().copyString(contents.data(), contents.size()));
}

Value fileReadLine(VM& vm, int, Value* args) {
  ObjFile* file = asFile(args[0]);
  if (!requireOpen(vm, file, "read_line()")) return nilValue();

  std::string line;
  int c;
  bool gotAny = false;
  while ((c = std::fgetc(file->handle)) != EOF) {
    gotAny = true;
    if (c == '\n') break;
    line += (char)c;
  }
  // nil rather than an empty string marks the end of the file, so a loop
  // can tell "blank line" from "no more lines".
  if (!gotAny) return nilValue();
  return objValue((Obj*)vm.runtime().copyString(line.data(), line.size()));
}

Value fileLines(VM& vm, int, Value* args) {
  ObjFile* file = asFile(args[0]);
  if (!requireOpen(vm, file, "lines()")) return nilValue();

  ObjArray* lines = vm.runtime().newArray();
  GCRoot linesRoot(vm.runtime(), (Obj*)lines);
  std::string line;
  int c;
  bool pending = false;
  while ((c = std::fgetc(file->handle)) != EOF) {
    pending = true;
    if (c == '\n') {
      lines->items.push_back(
          objValue((Obj*)vm.runtime().copyString(line.data(), line.size())));
      line.clear();
      pending = false;
      continue;
    }
    line += (char)c;
  }
  if (pending) {
    lines->items.push_back(
        objValue((Obj*)vm.runtime().copyString(line.data(), line.size())));
  }
  return objValue((Obj*)lines);
}

Value fileWrite(VM& vm, int argCount, Value* args) {
  ObjFile* file = asFile(args[0]);
  if (!requireOpen(vm, file, "write()")) return nilValue();
  std::string text;
  for (int i = 1; i < argCount; i++) text += valueToString(args[i]);
  size_t written = std::fwrite(text.data(), 1, text.size(), file->handle);
  return numberValue((double)written);
}

Value fileFlush(VM& vm, int, Value* args) {
  ObjFile* file = asFile(args[0]);
  if (!requireOpen(vm, file, "flush()")) return nilValue();
  std::fflush(file->handle);
  return args[0];
}

Value fileClose(VM&, int, Value* args) {
  ObjFile* file = asFile(args[0]);
  if (file->open && file->handle != nullptr) {
    std::fclose(file->handle);
    file->handle = nullptr;
    file->open = false;
  }
  return nilValue();
}

Value fileIsOpen(VM&, int, Value* args) {
  return boolValue(asFile(args[0])->open);
}

}  // namespace

// ---- directories ----------------------------------------------------

// The names inside a directory, sorted, without "." and "..". nil when
// the directory cannot be read, so a missing path and an empty directory
// are different answers.
Value nativeListDir(VM& vm, int argCount, Value* args) {
  ObjString* path = stringArg(vm, args[0], "list_dir()");
  if (path == nullptr) return nilValue();

  std::string directory(path->chars, path->length);
  DIR* handle = ::opendir(directory.c_str());
  if (handle == nullptr) return nilValue();

  std::vector<std::string> names;
  for (;;) {
    struct dirent* entry = ::readdir(handle);
    if (entry == nullptr) break;
    std::string name = entry->d_name;
    if (name == "." || name == "..") continue;
    names.push_back(name);
  }
  ::closedir(handle);

  // Sorted, so that a program that walks a directory does the same thing
  // twice running. The order readdir gives is whatever the filesystem
  // felt like.
  std::sort(names.begin(), names.end());

  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  result->items.reserve(names.size());
  for (const std::string& name : names) {
    result->items.push_back(
        objValue((Obj*)vm.runtime().copyString(name.data(), name.size())));
  }
  (void)argCount;
  return objValue((Obj*)result);
}

// Makes a directory and any parent it needs, like `mkdir -p`. A path
// that is already a directory is success, because the caller wanted it to
// exist and it does.
Value nativeMkdir(VM& vm, int, Value* args) {
  ObjString* path = stringArg(vm, args[0], "mkdir()");
  if (path == nullptr) return boolValue(false);
  std::string target(path->chars, path->length);
  if (target.empty()) return boolValue(false);

  std::string grown;
  for (size_t i = 0; i <= target.size(); i++) {
    if (i < target.size() && target[i] != '/') {
      grown += target[i];
      continue;
    }
    if (i < target.size()) grown += '/';
    // Skip the leading "/" of an absolute path, and any run of slashes.
    std::string step = grown;
    if (step.size() > 1 && step.back() == '/') step.pop_back();
    if (step.empty() || step == "/") continue;
    if (::mkdir(step.c_str(), 0777) != 0) {
      struct stat info;
      if (::stat(step.c_str(), &info) != 0 || !S_ISDIR(info.st_mode)) {
        return boolValue(false);
      }
    }
  }
  return boolValue(true);
}

// Removes a directory. It has to be empty: deleting a tree is a decision
// a program should make one file at a time.
Value nativeRemoveDir(VM& vm, int, Value* args) {
  ObjString* path = stringArg(vm, args[0], "remove_dir()");
  if (path == nullptr) return boolValue(false);
  std::string target(path->chars, path->length);
  return boolValue(::rmdir(target.c_str()) == 0);
}

Value nativeRename(VM& vm, int, Value* args) {
  ObjString* from = stringArg(vm, args[0], "rename()");
  if (from == nullptr) return boolValue(false);
  ObjString* to = stringArg(vm, args[1], "rename()");
  if (to == nullptr) return boolValue(false);
  return boolValue(std::rename(std::string(from->chars, from->length).c_str(),
                               std::string(to->chars, to->length).c_str()) == 0);
}

// Reads the kind and size of a path in one call. nil when there is
// nothing there, so `if (stat_of(p) == nil)` is the missing-file test.
bool statOf(VM& vm, Value value, const char* who, struct stat* out) {
  ObjString* path = stringArg(vm, value, who);
  if (path == nullptr) return false;
  std::string target(path->chars, path->length);
  return ::stat(target.c_str(), out) == 0;
}

Value nativeIsDir(VM& vm, int, Value* args) {
  struct stat info;
  if (!statOf(vm, args[0], "is_dir()", &info)) return boolValue(false);
  return boolValue(S_ISDIR(info.st_mode));
}

Value nativeIsFile(VM& vm, int, Value* args) {
  struct stat info;
  if (!statOf(vm, args[0], "is_file()", &info)) return boolValue(false);
  return boolValue(S_ISREG(info.st_mode));
}

Value nativeFileSize(VM& vm, int, Value* args) {
  struct stat info;
  if (!statOf(vm, args[0], "file_size()", &info)) return nilValue();
  return numberValue((double)info.st_size);
}

// When the path was last written, in the same seconds-since-the-epoch
// that time() reports.
Value nativeModified(VM& vm, int, Value* args) {
  struct stat info;
  if (!statOf(vm, args[0], "modified()", &info)) return nilValue();
  return numberValue((double)info.st_mtime);
}

void installIO(Runtime& runtime) {
  defineGlobalFn(runtime, "read_file", nativeReadFile, 1);
  defineGlobalFn(runtime, "write_file", nativeWriteFile, 2);
  defineGlobalFn(runtime, "append_file", nativeAppendFile, 2);
  defineGlobalFn(runtime, "open", nativeOpen, -1);
  defineGlobalFn(runtime, "remove_file", nativeRemoveFile, 1);

  defineGlobalFn(runtime, "list_dir", nativeListDir, 1);
  defineGlobalFn(runtime, "mkdir", nativeMkdir, 1);
  defineGlobalFn(runtime, "remove_dir", nativeRemoveDir, 1);
  defineGlobalFn(runtime, "rename", nativeRename, 2);
  defineGlobalFn(runtime, "is_dir", nativeIsDir, 1);
  defineGlobalFn(runtime, "is_file", nativeIsFile, 1);
  defineGlobalFn(runtime, "file_size", nativeFileSize, 1);
  defineGlobalFn(runtime, "modified", nativeModified, 1);

  defineMethodFn(runtime, ObjType::File, "read", fileRead, 1);
  defineMethodFn(runtime, ObjType::File, "read_line", fileReadLine, 1);
  defineMethodFn(runtime, ObjType::File, "lines", fileLines, 1);
  defineMethodFn(runtime, ObjType::File, "write", fileWrite, -1);
  defineMethodFn(runtime, ObjType::File, "flush", fileFlush, 1);
  defineMethodFn(runtime, ObjType::File, "close", fileClose, 1);
  defineMethodFn(runtime, ObjType::File, "is_open", fileIsOpen, 1);
}

}  // namespace red
