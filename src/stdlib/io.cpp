// File input and output.
#include <cstdio>

#include "../util.h"
#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

ObjString* stringArg(VM& vm, Value value, const char* who) {
  if (!isString(value)) {
    vm.fail("%s expects a string, got %s.", who, valueTypeName(value));
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
    return vm.fail("Cannot open '%s' in mode '%s'.", path->chars, mode.c_str());
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
    vm.fail("%s on a closed file.", who);
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

void installIO(Runtime& runtime) {
  defineGlobalFn(runtime, "read_file", nativeReadFile, 1);
  defineGlobalFn(runtime, "write_file", nativeWriteFile, 2);
  defineGlobalFn(runtime, "append_file", nativeAppendFile, 2);
  defineGlobalFn(runtime, "open", nativeOpen, -1);
  defineGlobalFn(runtime, "remove_file", nativeRemoveFile, 1);

  defineMethodFn(runtime, ObjType::File, "read", fileRead, 1);
  defineMethodFn(runtime, ObjType::File, "read_line", fileReadLine, 1);
  defineMethodFn(runtime, ObjType::File, "lines", fileLines, 1);
  defineMethodFn(runtime, ObjType::File, "write", fileWrite, -1);
  defineMethodFn(runtime, ObjType::File, "flush", fileFlush, 1);
  defineMethodFn(runtime, ObjType::File, "close", fileClose, 1);
  defineMethodFn(runtime, ObjType::File, "is_open", fileIsOpen, 1);
}

}  // namespace red
