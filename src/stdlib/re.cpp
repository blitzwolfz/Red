// Regular expressions, as Red sees them.
//
// The engine is in src/regex.cpp. This file is the binding: compiling a
// pattern into a value, and the six things a program does with one.
#include <string>
#include <vector>

#include "../regex.h"
#include "../util.h"
#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

// A match, as the map a program reads: where it was, what it covered,
// and what each group caught.
Value matchMap(VM& vm, const std::string& text, const ReMatch& found,
               int groupCount) {
  Runtime& rt = vm.runtime();
  ObjMap* map = rt.newMap();
  GCRoot mapRoot(rt, (Obj*)map);

  auto put = [&](const char* name, Value value) {
    ObjString* key = rt.internString(name);
    GCRoot keyRoot(rt, (Obj*)key);
    map->entries.set(objValue((Obj*)key), value);
  };

  int start = found.slots[0];
  int end = found.slots[1];
  put("start", numberValue(start));
  put("end", numberValue(end));

  ObjString* whole = rt.copyString(text.data() + start, (size_t)(end - start));
  GCRoot wholeRoot(rt, (Obj*)whole);
  put("text", objValue((Obj*)whole));

  // One entry per group, in order, with nil for a group that did not
  // take part in this match.
  ObjArray* groups = rt.newArray();
  GCRoot groupsRoot(rt, (Obj*)groups);
  for (int i = 1; i <= groupCount; i++) {
    int from = found.slots[(size_t)i * 2];
    int to = found.slots[(size_t)i * 2 + 1];
    if (from < 0 || to < from) {
      groups->items.push_back(nilValue());
      continue;
    }
    ObjString* piece =
        rt.copyString(text.data() + from, (size_t)(to - from));
    GCRoot pieceRoot(rt, (Obj*)piece);
    groups->items.push_back(objValue((Obj*)piece));
  }
  put("groups", objValue((Obj*)groups));
  return objValue((Obj*)map);
}

bool wantText(VM& vm, Value value, const char* who, std::string* out) {
  if (!isString(value)) {
    vm.failAs("type", "%s expects a string, got %s.", who,
              valueTypeName(value));
    return false;
  }
  out->assign(asString(value)->chars, asString(value)->length);
  return true;
}

// regex(pattern) or regex(pattern, flags).
Value nativeRegex(VM& vm, int argCount, Value* args) {
  std::string pattern;
  if (!wantText(vm, args[0], "regex()", &pattern)) return nilValue();

  std::string flags;
  if (argCount > 1 && !isNil(args[1])) {
    if (!wantText(vm, args[1], "regex()", &flags)) return nilValue();
  }

  Regex* program = new Regex();
  std::string reason;
  if (!program->compile(pattern, flags, &reason)) {
    delete program;
    return vm.failAs("regex", "Cannot compile /%s/: %s.", pattern.c_str(),
                     reason.c_str());
  }
  return objValue((Obj*)vm.runtime().newRegex(program));
}

// Where to start a search, as a byte offset, counting back from the end
// when it is negative.
bool startOffset(VM& vm, int argCount, Value* args, int index,
                 const std::string& text, size_t* out) {
  *out = 0;
  if (argCount <= index || isNil(args[index])) return true;
  if (!isNumber(args[index])) {
    vm.failAs("type", "The start position must be a number, got %s.",
              valueTypeName(args[index]));
    return false;
  }
  double where = asNumber(args[index]);
  if (where < 0) where += (double)text.size();
  if (where < 0) where = 0;
  if (where > (double)text.size()) where = (double)text.size();
  *out = (size_t)where;
  return true;
}

Value regexTest(VM& vm, int argCount, Value* args) {
  std::string text;
  if (!wantText(vm, args[1], "test()", &text)) return nilValue();
  size_t from = 0;
  if (!startOffset(vm, argCount, args, 2, text, &from)) return nilValue();
  return boolValue(asRegex(args[0])->program->search(text, from).matched);
}

Value regexFind(VM& vm, int argCount, Value* args) {
  std::string text;
  if (!wantText(vm, args[1], "find()", &text)) return nilValue();
  size_t from = 0;
  if (!startOffset(vm, argCount, args, 2, text, &from)) return nilValue();

  Regex* program = asRegex(args[0])->program;
  ReMatch found = program->search(text, from);
  if (!found.matched) return nilValue();
  return matchMap(vm, text, found, program->groupCount());
}

Value regexFindAll(VM& vm, int, Value* args) {
  std::string text;
  if (!wantText(vm, args[1], "find_all()", &text)) return nilValue();

  Regex* program = asRegex(args[0])->program;
  ObjArray* out = vm.runtime().newArray();
  GCRoot outRoot(vm.runtime(), (Obj*)out);

  size_t at = 0;
  for (;;) {
    ReMatch found = program->search(text, at);
    if (!found.matched) break;
    out->items.push_back(
        matchMap(vm, text, found, program->groupCount()));
    // An empty match would otherwise be found again in the same place
    // for ever, so the search moves on by one character.
    size_t end = (size_t)found.slots[1];
    if (end == (size_t)found.slots[0]) {
      if (end >= text.size()) break;
      uint32_t ignored;
      end += decodeUtf8(text.data(), text.size(), end, &ignored);
    }
    at = end;
    if (at > text.size()) break;
  }
  return objValue((Obj*)out);
}

// Expands $1 to $9, $0 and $$ in a replacement.
std::string expand(const std::string& replacement, const std::string& text,
                   const ReMatch& found, int groupCount) {
  std::string out;
  for (size_t i = 0; i < replacement.size(); i++) {
    if (replacement[i] != '$' || i + 1 >= replacement.size()) {
      out += replacement[i];
      continue;
    }
    char next = replacement[i + 1];
    if (next == '$') {
      out += '$';
      i++;
      continue;
    }
    if (next < '0' || next > '9') {
      out += replacement[i];
      continue;
    }
    int group = next - '0';
    i++;
    if (group > groupCount) continue;
    int from = found.slots[(size_t)group * 2];
    int to = found.slots[(size_t)group * 2 + 1];
    if (from >= 0 && to >= from) out.append(text, (size_t)from, (size_t)(to - from));
  }
  return out;
}

// replace(text, replacement) changes every match.
// replace(text, replacement, count) stops after count of them.
Value regexReplace(VM& vm, int argCount, Value* args) {
  std::string text;
  if (!wantText(vm, args[1], "replace()", &text)) return nilValue();
  std::string replacement;
  if (!wantText(vm, args[2], "replace()", &replacement)) return nilValue();

  long limit = -1;
  if (argCount > 3 && !isNil(args[3])) {
    if (!isNumber(args[3])) {
      return vm.failAs("type", "replace() expects a count, got %s.",
                       valueTypeName(args[3]));
    }
    limit = (long)asNumber(args[3]);
  }

  Regex* program = asRegex(args[0])->program;
  std::string out;
  size_t at = 0;
  long done = 0;
  while (limit < 0 || done < limit) {
    ReMatch found = program->search(text, at);
    if (!found.matched) break;
    size_t start = (size_t)found.slots[0];
    size_t end = (size_t)found.slots[1];
    out.append(text, at, start - at);
    out += expand(replacement, text, found, program->groupCount());
    done++;

    if (end == start) {
      // An empty match: copy one character across and move past it, so
      // that the same position is not matched for ever.
      if (end >= text.size()) {
        at = end;
        break;
      }
      uint32_t ignored;
      size_t width = decodeUtf8(text.data(), text.size(), end, &ignored);
      out.append(text, end, width);
      at = end + width;
    } else {
      at = end;
    }
    if (at > text.size()) break;
  }
  if (at < text.size()) out.append(text, at, text.size() - at);
  return objValue((Obj*)vm.runtime().copyString(out.data(), out.size()));
}

// Splits on every match. A capture group in the pattern is kept, the way
// it is in most languages, so that what the pieces were split on is not
// lost.
Value regexSplit(VM& vm, int, Value* args) {
  std::string text;
  if (!wantText(vm, args[1], "split()", &text)) return nilValue();

  Regex* program = asRegex(args[0])->program;
  ObjArray* out = vm.runtime().newArray();
  GCRoot outRoot(vm.runtime(), (Obj*)out);

  auto pushPiece = [&](size_t from, size_t to) {
    ObjString* piece = vm.runtime().copyString(text.data() + from, to - from);
    GCRoot pieceRoot(vm.runtime(), (Obj*)piece);
    out->items.push_back(objValue((Obj*)piece));
  };

  size_t at = 0;
  size_t search = 0;
  for (;;) {
    ReMatch found = program->search(text, search);
    if (!found.matched) break;
    size_t start = (size_t)found.slots[0];
    size_t end = (size_t)found.slots[1];

    if (end == start) {
      // An empty separator would split between every character and never
      // finish, so it is stepped past instead.
      if (end >= text.size()) break;
      uint32_t ignored;
      search = end + decodeUtf8(text.data(), text.size(), end, &ignored);
      continue;
    }

    pushPiece(at, start);
    for (int i = 1; i <= program->groupCount(); i++) {
      int from = found.slots[(size_t)i * 2];
      int to = found.slots[(size_t)i * 2 + 1];
      if (from < 0 || to < from) {
        out->items.push_back(nilValue());
      } else {
        pushPiece((size_t)from, (size_t)to);
      }
    }
    at = end;
    search = end;
  }
  pushPiece(at, text.size());
  return objValue((Obj*)out);
}

Value regexPattern(VM& vm, int, Value* args) {
  const std::string& pattern = asRegex(args[0])->program->pattern();
  return objValue(
      (Obj*)vm.runtime().copyString(pattern.data(), pattern.size()));
}

Value regexFlags(VM& vm, int, Value* args) {
  const std::string& flags = asRegex(args[0])->program->flags();
  return objValue((Obj*)vm.runtime().copyString(flags.data(), flags.size()));
}

Value regexGroups(VM&, int, Value* args) {
  return numberValue(asRegex(args[0])->program->groupCount());
}

}  // namespace

void installRegex(Runtime& runtime) {
  defineGlobalFn(runtime, "regex", nativeRegex, -1);

  defineMethodFn(runtime, ObjType::Regex, "test", regexTest, -1);
  defineMethodFn(runtime, ObjType::Regex, "find", regexFind, -1);
  defineMethodFn(runtime, ObjType::Regex, "find_all", regexFindAll, 2);
  defineMethodFn(runtime, ObjType::Regex, "replace", regexReplace, -1);
  defineMethodFn(runtime, ObjType::Regex, "split", regexSplit, 2);
  defineMethodFn(runtime, ObjType::Regex, "pattern", regexPattern, 1);
  defineMethodFn(runtime, ObjType::Regex, "flags", regexFlags, 1);
  defineMethodFn(runtime, ObjType::Regex, "groups", regexGroups, 1);
}

}  // namespace red
