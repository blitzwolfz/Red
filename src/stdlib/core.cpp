// Core functions and the methods on strings, arrays and maps.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>

#include "../vm.h"
#include "builtins.h"

namespace red {

namespace {

bool wantString(VM& vm, Value value, const char* who, ObjString** out) {
  if (!isString(value)) {
    vm.fail("%s expects a string, got %s.", who, valueTypeName(value));
    return false;
  }
  *out = asString(value);
  return true;
}

bool wantNumber(VM& vm, Value value, const char* who, double* out) {
  if (!isNumber(value)) {
    vm.fail("%s expects a number, got %s.", who, valueTypeName(value));
    return false;
  }
  *out = asNumber(value);
  return true;
}

std::string textOf(ObjString* s) { return std::string(s->chars, s->length); }

// ---- global functions ----------------------------------------------

Value nativePrint(VM& vm, int argCount, Value* args) {
  std::string line;
  for (int i = 0; i < argCount; i++) {
    if (i > 0) line += " ";
    line += valueToString(args[i]);
  }
  line += "\n";
  // One write per call keeps output from two tasks from interleaving in
  // the middle of a line.
  std::fwrite(line.data(), 1, line.size(), stdout);
  (void)vm;
  return nilValue();
}

Value nativeWrite(VM& vm, int argCount, Value* args) {
  std::string text;
  for (int i = 0; i < argCount; i++) text += valueToString(args[i]);
  std::fwrite(text.data(), 1, text.size(), stdout);
  (void)vm;
  return nilValue();
}

Value nativeClock(VM&, int, Value*) {
  return numberValue((double)std::clock() / CLOCKS_PER_SEC);
}

Value nativeTypeName(VM& vm, int, Value* args) {
  return objValue((Obj*)vm.runtime().internString(valueTypeName(args[0])));
}

Value nativeStr(VM& vm, int, Value* args) {
  return objValue((Obj*)vm.runtime().internString(valueToString(args[0])));
}

Value nativeRepr(VM& vm, int, Value* args) {
  return objValue((Obj*)vm.runtime().internString(valueToDisplay(args[0])));
}

Value nativeNum(VM& vm, int, Value* args) {
  if (isNumber(args[0])) return args[0];
  ObjString* text;
  if (!wantString(vm, args[0], "num()", &text)) return nilValue();
  std::string trimmed = textOf(text);
  char* end = nullptr;
  double value = std::strtod(trimmed.c_str(), &end);
  // Anything left over means the whole string was not a number, which is
  // reported as nil rather than as a partial parse.
  if (end == trimmed.c_str() || *end != '\0') return nilValue();
  return numberValue(value);
}

Value nativeInt(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "int()", &value)) return nilValue();
  return numberValue(std::trunc(value));
}

Value nativeLen(VM& vm, int, Value* args) {
  Value value = args[0];
  if (isString(value)) return numberValue((double)asString(value)->length);
  if (isArray(value)) return numberValue((double)asArray(value)->items.size());
  if (isMap(value)) return numberValue((double)asMap(value)->entries.count());
  return vm.fail("len() expects a string, array or map, got %s.",
                 valueTypeName(value));
}

Value nativeAssert(VM& vm, int argCount, Value* args) {
  if (!isFalsey(args[0])) return nilValue();
  if (argCount > 1) {
    return vm.fail("Assertion failed: %s", valueToString(args[1]).c_str());
  }
  return vm.fail("Assertion failed.");
}

Value nativeError(VM& vm, int argCount, Value* args) {
  Runtime& rt = vm.runtime();
  ObjString* message = rt.internString(valueToString(args[0]));
  GCRoot messageRoot(rt, (Obj*)message);
  ObjString* trace = rt.internString(vm.buildTrace());
  Value payload = argCount > 1 ? args[1] : nilValue();
  return objValue((Obj*)rt.newError(message, trace, payload));
}

Value nativeRange(VM& vm, int argCount, Value* args) {
  double start = 0, stop = 0, step = 1;
  if (argCount == 1) {
    if (!wantNumber(vm, args[0], "range()", &stop)) return nilValue();
  } else {
    if (!wantNumber(vm, args[0], "range()", &start)) return nilValue();
    if (!wantNumber(vm, args[1], "range()", &stop)) return nilValue();
    if (argCount > 2 && !wantNumber(vm, args[2], "range()", &step)) {
      return nilValue();
    }
  }
  if (step == 0) return vm.fail("range() step cannot be zero.");

  ObjArray* array = vm.runtime().newArray();
  GCRoot arrayRoot(vm.runtime(), (Obj*)array);
  if (step > 0) {
    for (double i = start; i < stop; i += step) array->items.push_back(numberValue(i));
  } else {
    for (double i = start; i > stop; i += step) array->items.push_back(numberValue(i));
  }
  return objValue((Obj*)array);
}

Value nativeInput(VM& vm, int argCount, Value* args) {
  if (argCount > 0) {
    std::string prompt = valueToString(args[0]);
    std::fwrite(prompt.data(), 1, prompt.size(), stdout);
    std::fflush(stdout);
  }
  std::string line;
  int c;
  // Reading stdin can block, so the runtime lock is dropped first. No heap
  // access happens while it is released.
  vm.releaseLock();
  bool gotAny = false;
  while ((c = std::fgetc(stdin)) != EOF) {
    gotAny = true;
    if (c == '\n') break;
    line += (char)c;
  }
  vm.acquireLock();
  if (!gotAny) return nilValue();
  return objValue((Obj*)vm.runtime().copyString(line.data(), line.size()));
}

Value nativeAbs(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "abs()", &value)) return nilValue();
  return numberValue(std::fabs(value));
}

Value nativeFloor(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "floor()", &value)) return nilValue();
  return numberValue(std::floor(value));
}

Value nativeCeil(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "ceil()", &value)) return nilValue();
  return numberValue(std::ceil(value));
}

Value nativeSqrt(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "sqrt()", &value)) return nilValue();
  if (value < 0) return vm.fail("sqrt() of a negative number.");
  return numberValue(std::sqrt(value));
}

Value nativePow(VM& vm, int, Value* args) {
  double base, exponent;
  if (!wantNumber(vm, args[0], "pow()", &base)) return nilValue();
  if (!wantNumber(vm, args[1], "pow()", &exponent)) return nilValue();
  return numberValue(std::pow(base, exponent));
}

Value nativeMin(VM& vm, int argCount, Value* args) {
  double best;
  if (!wantNumber(vm, args[0], "min()", &best)) return nilValue();
  for (int i = 1; i < argCount; i++) {
    double value;
    if (!wantNumber(vm, args[i], "min()", &value)) return nilValue();
    if (value < best) best = value;
  }
  return numberValue(best);
}

Value nativeMax(VM& vm, int argCount, Value* args) {
  double best;
  if (!wantNumber(vm, args[0], "max()", &best)) return nilValue();
  for (int i = 1; i < argCount; i++) {
    double value;
    if (!wantNumber(vm, args[i], "max()", &value)) return nilValue();
    if (value > best) best = value;
  }
  return numberValue(best);
}

// ---- string methods -------------------------------------------------

Value stringLen(VM&, int, Value* args) {
  return numberValue((double)asString(args[0])->length);
}

Value stringUpper(VM& vm, int, Value* args) {
  std::string text = textOf(asString(args[0]));
  for (char& c : text) c = (char)std::toupper((unsigned char)c);
  return objValue((Obj*)vm.runtime().copyString(text.data(), text.size()));
}

Value stringLower(VM& vm, int, Value* args) {
  std::string text = textOf(asString(args[0]));
  for (char& c : text) c = (char)std::tolower((unsigned char)c);
  return objValue((Obj*)vm.runtime().copyString(text.data(), text.size()));
}

Value stringTrim(VM& vm, int, Value* args) {
  std::string text = textOf(asString(args[0]));
  size_t first = text.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return objValue((Obj*)vm.runtime().internString(""));
  }
  size_t last = text.find_last_not_of(" \t\r\n");
  std::string result = text.substr(first, last - first + 1);
  return objValue((Obj*)vm.runtime().copyString(result.data(), result.size()));
}

Value stringSplit(VM& vm, int, Value* args) {
  ObjString* separator;
  if (!wantString(vm, args[1], "split()", &separator)) return nilValue();
  std::string text = textOf(asString(args[0]));
  std::string sep = textOf(separator);

  ObjArray* array = vm.runtime().newArray();
  GCRoot arrayRoot(vm.runtime(), (Obj*)array);
  if (sep.empty()) {
    // Splitting on the empty string yields the individual characters.
    for (char c : text) {
      array->items.push_back(objValue((Obj*)vm.runtime().copyString(&c, 1)));
    }
    return objValue((Obj*)array);
  }
  size_t start = 0;
  for (;;) {
    size_t found = text.find(sep, start);
    if (found == std::string::npos) break;
    std::string piece = text.substr(start, found - start);
    array->items.push_back(
        objValue((Obj*)vm.runtime().copyString(piece.data(), piece.size())));
    start = found + sep.size();
  }
  std::string tail = text.substr(start);
  array->items.push_back(
      objValue((Obj*)vm.runtime().copyString(tail.data(), tail.size())));
  return objValue((Obj*)array);
}

Value stringFind(VM& vm, int, Value* args) {
  ObjString* needle;
  if (!wantString(vm, args[1], "find()", &needle)) return nilValue();
  size_t found = textOf(asString(args[0])).find(textOf(needle));
  return numberValue(found == std::string::npos ? -1 : (double)found);
}

Value stringContains(VM& vm, int, Value* args) {
  ObjString* needle;
  if (!wantString(vm, args[1], "contains()", &needle)) return nilValue();
  return boolValue(textOf(asString(args[0])).find(textOf(needle)) !=
                   std::string::npos);
}

Value stringStartsWith(VM& vm, int, Value* args) {
  ObjString* prefix;
  if (!wantString(vm, args[1], "starts_with()", &prefix)) return nilValue();
  return boolValue(textOf(asString(args[0])).rfind(textOf(prefix), 0) == 0);
}

Value stringEndsWith(VM& vm, int, Value* args) {
  ObjString* suffix;
  if (!wantString(vm, args[1], "ends_with()", &suffix)) return nilValue();
  std::string text = textOf(asString(args[0]));
  std::string tail = textOf(suffix);
  if (tail.size() > text.size()) return boolValue(false);
  return boolValue(text.compare(text.size() - tail.size(), tail.size(), tail) ==
                   0);
}

Value stringSub(VM& vm, int argCount, Value* args) {
  std::string text = textOf(asString(args[0]));
  double startRaw;
  if (!wantNumber(vm, args[1], "sub()", &startRaw)) return nilValue();
  long start = (long)startRaw;
  if (start < 0) start += (long)text.size();
  if (start < 0) start = 0;
  if (start > (long)text.size()) start = (long)text.size();

  long end = (long)text.size();
  if (argCount > 2) {
    double endRaw;
    if (!wantNumber(vm, args[2], "sub()", &endRaw)) return nilValue();
    end = (long)endRaw;
    if (end < 0) end += (long)text.size();
  }
  if (end > (long)text.size()) end = (long)text.size();
  if (end < start) end = start;

  std::string piece = text.substr((size_t)start, (size_t)(end - start));
  return objValue((Obj*)vm.runtime().copyString(piece.data(), piece.size()));
}

Value stringReplace(VM& vm, int, Value* args) {
  ObjString* from;
  ObjString* to;
  if (!wantString(vm, args[1], "replace()", &from)) return nilValue();
  if (!wantString(vm, args[2], "replace()", &to)) return nilValue();
  std::string text = textOf(asString(args[0]));
  std::string needle = textOf(from);
  std::string replacement = textOf(to);
  if (needle.empty()) return args[0];

  std::string out;
  size_t start = 0;
  for (;;) {
    size_t found = text.find(needle, start);
    if (found == std::string::npos) break;
    out += text.substr(start, found - start);
    out += replacement;
    start = found + needle.size();
  }
  out += text.substr(start);
  return objValue((Obj*)vm.runtime().copyString(out.data(), out.size()));
}

Value stringRepeat(VM& vm, int, Value* args) {
  double count;
  if (!wantNumber(vm, args[1], "repeat()", &count)) return nilValue();
  if (count < 0) return vm.fail("repeat() count cannot be negative.");
  std::string text = textOf(asString(args[0]));
  std::string out;
  out.reserve(text.size() * (size_t)count);
  for (long i = 0; i < (long)count; i++) out += text;
  return objValue((Obj*)vm.runtime().copyString(out.data(), out.size()));
}

// ---- array methods --------------------------------------------------

Value arrayLen(VM&, int, Value* args) {
  return numberValue((double)asArray(args[0])->items.size());
}

Value arrayPush(VM&, int argCount, Value* args) {
  ObjArray* array = asArray(args[0]);
  for (int i = 1; i < argCount; i++) array->items.push_back(args[i]);
  return args[0];
}

Value arrayPop(VM& vm, int, Value* args) {
  ObjArray* array = asArray(args[0]);
  if (array->items.empty()) return vm.fail("pop() on an empty array.");
  Value value = array->items.back();
  array->items.pop_back();
  return value;
}

Value arrayInsert(VM& vm, int, Value* args) {
  ObjArray* array = asArray(args[0]);
  double indexRaw;
  if (!wantNumber(vm, args[1], "insert()", &indexRaw)) return nilValue();
  long index = (long)indexRaw;
  if (index < 0) index += (long)array->items.size();
  if (index < 0 || index > (long)array->items.size()) {
    return vm.fail("insert() index %ld out of range.", (long)indexRaw);
  }
  array->items.insert(array->items.begin() + index, args[2]);
  return args[0];
}

Value arrayRemove(VM& vm, int, Value* args) {
  ObjArray* array = asArray(args[0]);
  double indexRaw;
  if (!wantNumber(vm, args[1], "remove()", &indexRaw)) return nilValue();
  long index = (long)indexRaw;
  if (index < 0) index += (long)array->items.size();
  if (index < 0 || index >= (long)array->items.size()) {
    return vm.fail("remove() index %ld out of range.", (long)indexRaw);
  }
  Value value = array->items[(size_t)index];
  array->items.erase(array->items.begin() + index);
  return value;
}

Value arraySlice(VM& vm, int argCount, Value* args) {
  ObjArray* source = asArray(args[0]);
  long size = (long)source->items.size();
  double startRaw;
  if (!wantNumber(vm, args[1], "slice()", &startRaw)) return nilValue();
  long start = (long)startRaw;
  if (start < 0) start += size;
  if (start < 0) start = 0;
  if (start > size) start = size;

  long end = size;
  if (argCount > 2) {
    double endRaw;
    if (!wantNumber(vm, args[2], "slice()", &endRaw)) return nilValue();
    end = (long)endRaw;
    if (end < 0) end += size;
  }
  if (end > size) end = size;
  if (end < start) end = start;

  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  result->items.assign(source->items.begin() + start,
                       source->items.begin() + end);
  return objValue((Obj*)result);
}

Value arrayJoin(VM& vm, int argCount, Value* args) {
  std::string separator;
  if (argCount > 1) {
    ObjString* sep;
    if (!wantString(vm, args[1], "join()", &sep)) return nilValue();
    separator = textOf(sep);
  }
  ObjArray* array = asArray(args[0]);
  std::string out;
  for (size_t i = 0; i < array->items.size(); i++) {
    if (i > 0) out += separator;
    out += valueToString(array->items[i]);
  }
  return objValue((Obj*)vm.runtime().copyString(out.data(), out.size()));
}

Value arrayContains(VM&, int, Value* args) {
  for (Value item : asArray(args[0])->items) {
    if (valuesEqual(item, args[1])) return boolValue(true);
  }
  return boolValue(false);
}

Value arrayIndexOf(VM&, int, Value* args) {
  ObjArray* array = asArray(args[0]);
  for (size_t i = 0; i < array->items.size(); i++) {
    if (valuesEqual(array->items[i], args[1])) return numberValue((double)i);
  }
  return numberValue(-1);
}

Value arrayReverse(VM&, int, Value* args) {
  ObjArray* array = asArray(args[0]);
  std::reverse(array->items.begin(), array->items.end());
  return args[0];
}

Value arrayClear(VM&, int, Value* args) {
  asArray(args[0])->items.clear();
  return args[0];
}

Value arraySort(VM& vm, int argCount, Value* args) {
  ObjArray* array = asArray(args[0]);
  if (argCount == 1) {
    // Default order: numbers before strings, each group ordered naturally.
    bool bad = false;
    std::stable_sort(array->items.begin(), array->items.end(),
                     [&](Value a, Value b) {
                       if (isNumber(a) && isNumber(b)) {
                         return asNumber(a) < asNumber(b);
                       }
                       if (isString(a) && isString(b)) {
                         return textOf(asString(a)) < textOf(asString(b));
                       }
                       bad = true;
                       return false;
                     });
    if (bad) {
      return vm.fail("sort() needs all numbers or all strings.");
    }
    return args[0];
  }

  // With a comparator, each comparison re-enters the interpreter. A copy
  // is sorted so that a comparator that mutates the array cannot corrupt
  // the sort in progress.
  std::vector<Value> copy = array->items;
  bool failed = false;
  std::stable_sort(copy.begin(), copy.end(), [&](Value a, Value b) {
    if (failed) return false;
    vm.push(args[1]);
    vm.push(a);
    vm.push(b);
    Value result = nilValue();
    if (vm.callAndRun(args[1], 2, &result) != InterpretResult::Ok) {
      failed = true;
      return false;
    }
    return !isFalsey(result);
  });
  if (failed) {
    return vm.fail("sort() comparator failed.");
  }
  array->items = copy;
  return args[0];
}

Value arrayMap(VM& vm, int, Value* args) {
  ObjArray* source = asArray(args[0]);
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  result->items.reserve(source->items.size());

  for (size_t i = 0; i < source->items.size(); i++) {
    vm.push(args[1]);
    vm.push(source->items[i]);
    Value mapped = nilValue();
    if (vm.callAndRun(args[1], 1, &mapped) != InterpretResult::Ok) {
      return vm.fail("map() callback failed.");
    }
    result->items.push_back(mapped);
  }
  return objValue((Obj*)result);
}

Value arrayFilter(VM& vm, int, Value* args) {
  ObjArray* source = asArray(args[0]);
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);

  for (size_t i = 0; i < source->items.size(); i++) {
    vm.push(args[1]);
    vm.push(source->items[i]);
    Value keep = nilValue();
    if (vm.callAndRun(args[1], 1, &keep) != InterpretResult::Ok) {
      return vm.fail("filter() callback failed.");
    }
    if (!isFalsey(keep)) result->items.push_back(source->items[i]);
  }
  return objValue((Obj*)result);
}

Value arrayReduce(VM& vm, int argCount, Value* args) {
  ObjArray* source = asArray(args[0]);
  size_t start = 0;
  Value accumulator;
  if (argCount > 2) {
    accumulator = args[2];
  } else {
    if (source->items.empty()) {
      return vm.fail("reduce() on an empty array needs a starting value.");
    }
    accumulator = source->items[0];
    start = 1;
  }

  for (size_t i = start; i < source->items.size(); i++) {
    vm.push(args[1]);
    vm.push(accumulator);
    vm.push(source->items[i]);
    Value next = nilValue();
    if (vm.callAndRun(args[1], 2, &next) != InterpretResult::Ok) {
      return vm.fail("reduce() callback failed.");
    }
    accumulator = next;
  }
  return accumulator;
}

// ---- map methods ----------------------------------------------------

Value mapLen(VM&, int, Value* args) {
  return numberValue((double)asMap(args[0])->entries.count());
}

Value mapGet(VM&, int argCount, Value* args) {
  Value value;
  if (asMap(args[0])->entries.get(args[1], &value)) return value;
  return argCount > 2 ? args[2] : nilValue();
}

Value mapSet(VM& vm, int, Value* args) {
  if (isObj(args[1]) && !isString(args[1])) {
    return vm.fail("Map keys must be strings, numbers, booleans or nil.");
  }
  asMap(args[0])->entries.set(args[1], args[2]);
  return args[0];
}

Value mapHas(VM&, int, Value* args) {
  Value ignored;
  return boolValue(asMap(args[0])->entries.get(args[1], &ignored));
}

Value mapRemove(VM&, int, Value* args) {
  return boolValue(asMap(args[0])->entries.remove(args[1]));
}

Value mapKeys(VM& vm, int, Value* args) {
  ObjArray* keys = vm.runtime().newArray();
  GCRoot keysRoot(vm.runtime(), (Obj*)keys);
  for (const ValueEntry& slot : asMap(args[0])->entries.slots()) {
    if (slot.used) keys->items.push_back(slot.key);
  }
  return objValue((Obj*)keys);
}

Value mapValues(VM& vm, int, Value* args) {
  ObjArray* values = vm.runtime().newArray();
  GCRoot valuesRoot(vm.runtime(), (Obj*)values);
  for (const ValueEntry& slot : asMap(args[0])->entries.slots()) {
    if (slot.used) values->items.push_back(slot.value);
  }
  return objValue((Obj*)values);
}

}  // namespace

void installCore(Runtime& runtime) {
  defineGlobalFn(runtime, "print", nativePrint, -1);
  defineGlobalFn(runtime, "write", nativeWrite, -1);
  defineGlobalFn(runtime, "clock", nativeClock, 0);
  defineGlobalFn(runtime, "type", nativeTypeName, 1);
  defineGlobalFn(runtime, "str", nativeStr, 1);
  defineGlobalFn(runtime, "repr", nativeRepr, 1);
  defineGlobalFn(runtime, "num", nativeNum, 1);
  defineGlobalFn(runtime, "int", nativeInt, 1);
  defineGlobalFn(runtime, "len", nativeLen, 1);
  defineGlobalFn(runtime, "assert", nativeAssert, -1);
  defineGlobalFn(runtime, "error", nativeError, -1);
  defineGlobalFn(runtime, "range", nativeRange, -1);
  defineGlobalFn(runtime, "input", nativeInput, -1);
  defineGlobalFn(runtime, "abs", nativeAbs, 1);
  defineGlobalFn(runtime, "floor", nativeFloor, 1);
  defineGlobalFn(runtime, "ceil", nativeCeil, 1);
  defineGlobalFn(runtime, "sqrt", nativeSqrt, 1);
  defineGlobalFn(runtime, "pow", nativePow, 2);
  defineGlobalFn(runtime, "min", nativeMin, -1);
  defineGlobalFn(runtime, "max", nativeMax, -1);

  defineMethodFn(runtime, ObjType::String, "len", stringLen, 1);
  defineMethodFn(runtime, ObjType::String, "upper", stringUpper, 1);
  defineMethodFn(runtime, ObjType::String, "lower", stringLower, 1);
  defineMethodFn(runtime, ObjType::String, "trim", stringTrim, 1);
  defineMethodFn(runtime, ObjType::String, "split", stringSplit, 2);
  defineMethodFn(runtime, ObjType::String, "find", stringFind, 2);
  defineMethodFn(runtime, ObjType::String, "contains", stringContains, 2);
  defineMethodFn(runtime, ObjType::String, "starts_with", stringStartsWith, 2);
  defineMethodFn(runtime, ObjType::String, "ends_with", stringEndsWith, 2);
  defineMethodFn(runtime, ObjType::String, "sub", stringSub, -1);
  defineMethodFn(runtime, ObjType::String, "replace", stringReplace, 3);
  defineMethodFn(runtime, ObjType::String, "repeat", stringRepeat, 2);

  defineMethodFn(runtime, ObjType::Array, "len", arrayLen, 1);
  defineMethodFn(runtime, ObjType::Array, "push", arrayPush, -1);
  defineMethodFn(runtime, ObjType::Array, "pop", arrayPop, 1);
  defineMethodFn(runtime, ObjType::Array, "insert", arrayInsert, 3);
  defineMethodFn(runtime, ObjType::Array, "remove", arrayRemove, 2);
  defineMethodFn(runtime, ObjType::Array, "slice", arraySlice, -1);
  defineMethodFn(runtime, ObjType::Array, "join", arrayJoin, -1);
  defineMethodFn(runtime, ObjType::Array, "contains", arrayContains, 2);
  defineMethodFn(runtime, ObjType::Array, "index_of", arrayIndexOf, 2);
  defineMethodFn(runtime, ObjType::Array, "reverse", arrayReverse, 1);
  defineMethodFn(runtime, ObjType::Array, "clear", arrayClear, 1);
  defineMethodFn(runtime, ObjType::Array, "sort", arraySort, -1);
  defineMethodFn(runtime, ObjType::Array, "map", arrayMap, 2);
  defineMethodFn(runtime, ObjType::Array, "filter", arrayFilter, 2);
  defineMethodFn(runtime, ObjType::Array, "reduce", arrayReduce, -1);

  defineMethodFn(runtime, ObjType::Map, "len", mapLen, 1);
  defineMethodFn(runtime, ObjType::Map, "get", mapGet, -1);
  defineMethodFn(runtime, ObjType::Map, "set", mapSet, 3);
  defineMethodFn(runtime, ObjType::Map, "has", mapHas, 2);
  defineMethodFn(runtime, ObjType::Map, "remove", mapRemove, 2);
  defineMethodFn(runtime, ObjType::Map, "keys", mapKeys, 1);
  defineMethodFn(runtime, ObjType::Map, "values", mapValues, 1);
}

}  // namespace red
