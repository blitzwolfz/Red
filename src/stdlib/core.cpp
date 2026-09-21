// Core functions and the methods on strings, arrays and maps.
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <random>

#include "../types.h"
#include "../unicode.h"
#include "../util.h"
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
    line += vm.stringify(args[i]);
  }
  line += "\n";
  // One write per call keeps output from two tasks from interleaving in
  // the middle of a line.
  std::fwrite(line.data(), 1, line.size(), stdout);
  (void)vm;
  return nilValue();
}

// The same as print and write, on the error stream. A program's results
// go to stdout and its complaints go to stderr, so the two can be
// separated by whoever runs it.
Value nativeEprint(VM& vm, int argCount, Value* args) {
  std::string line;
  for (int i = 0; i < argCount; i++) {
    if (i > 0) line += " ";
    line += vm.stringify(args[i]);
  }
  line += "\n";
  std::fwrite(line.data(), 1, line.size(), stderr);
  (void)vm;
  return nilValue();
}

Value nativeEwrite(VM& vm, int argCount, Value* args) {
  std::string text;
  for (int i = 0; i < argCount; i++) text += vm.stringify(args[i]);
  std::fwrite(text.data(), 1, text.size(), stderr);
  (void)vm;
  return nilValue();
}

Value nativeWrite(VM& vm, int argCount, Value* args) {
  std::string text;
  for (int i = 0; i < argCount; i++) text += vm.stringify(args[i]);
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
  return objValue((Obj*)vm.runtime().copyString(vm.stringify(args[0])));
}

Value nativeRepr(VM& vm, int, Value* args) {
  return objValue((Obj*)vm.runtime().copyString(vm.display(args[0])));
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

// The type of a value, as a value. Built fresh each time, so two calls
// give two objects that describe the same thing; compare them with == or
// ask the question directly with `is`.
Value nativeTypeOf(VM& vm, int, Value* args) {
  return objValue((Obj*)typeOf(vm.runtime(), args[0]));
}

Value nativeLen(VM& vm, int, Value* args) {
  Value value = args[0];
  if (isString(value)) return numberValue((double)asString(value)->length);
  if (isArray(value)) return numberValue((double)asArray(value)->items.size());
  if (isMap(value)) return numberValue((double)asMap(value)->entries.count());
  if (isSet(value)) return numberValue((double)asSet(value)->entries.count());
  if (isEnum(value)) return numberValue((double)asEnum(value)->ordered.size());
  return vm.failAs("type",
                   "len() expects a string, array, map, set or enum, got %s.",
                   valueTypeName(value));
}

Value nativeAssert(VM& vm, int argCount, Value* args) {
  if (!isFalsey(args[0])) return nilValue();
  if (argCount > 1) {
    return vm.failAs("assert", "Assertion failed: %s",
                     valueToString(args[1]).c_str());
  }
  return vm.failAs("assert", "Assertion failed.");
}

// error(message), error(message, payload) or error(message, payload, kind).
// The kind defaults to "user", which is what separates a thrown error
// from one the runtime raised.
Value nativeError(VM& vm, int argCount, Value* args) {
  Value payload = argCount > 1 ? args[1] : nilValue();
  std::string kind = "user";
  if (argCount > 2) {
    ObjString* given;
    if (!wantString(vm, args[2], "error()", &given)) return nilValue();
    kind.assign(given->chars, given->length);
  }
  return vm.makeError(kind.c_str(), vm.stringify(args[0]), payload);
}

// Turns a byte value into a one character string. This is what lets Red
// build binary output, which a self-hosted compiler needs in order to
// write a compiled file.
Value nativeChr(VM& vm, int, Value* args) {
  double code;
  if (!wantNumber(vm, args[0], "chr()", &code)) return nilValue();
  if (code < 0 || code > 255 || code != std::floor(code)) {
    return vm.fail("chr() expects a whole number from 0 to 255, got %s.",
                   valueToString(args[0]).c_str());
  }
  char byte = (char)(unsigned char)code;
  return objValue((Obj*)vm.runtime().copyString(&byte, 1));
}

// The character counterpart of chr(). chr() builds one byte; this builds
// one character, which is one to four bytes of UTF-8.
Value nativeChar(VM& vm, int, Value* args) {
  double code;
  if (!wantNumber(vm, args[0], "char()", &code)) return nilValue();
  if (code < 0 || code > (double)kMaxCodePoint || code != std::floor(code)) {
    return vm.fail("char() expects a code point from 0 to %u, got %s.",
                   kMaxCodePoint, valueToString(args[0]).c_str());
  }
  char buffer[4];
  size_t written = encodeUtf8((uint32_t)code, buffer);
  if (written == 0) {
    return vm.fail("char() cannot encode %s: it is half of a surrogate pair.",
                   valueToString(args[0]).c_str());
  }
  return objValue((Obj*)vm.runtime().copyString(buffer, written));
}

// One generator for the process. Natives run while their task holds the
// runtime lock, so no two tasks are ever inside this at once.
std::mt19937_64& generator() {
  static std::mt19937_64 engine(std::random_device{}());
  return engine;
}

// rand() gives a fraction. rand(n) and rand(a, b) give whole numbers, and
// the upper end is excluded so that rand(n) indexes an array of n things
// and rand(a, b) covers the same values as range(a, b).
Value nativeRand(VM& vm, int argCount, Value* args) {
  if (argCount == 0) {
    return numberValue(
        std::uniform_real_distribution<double>(0.0, 1.0)(generator()));
  }

  double low = 0;
  double high;
  if (argCount == 1) {
    if (!wantNumber(vm, args[0], "rand()", &high)) return nilValue();
  } else {
    if (!wantNumber(vm, args[0], "rand()", &low)) return nilValue();
    if (!wantNumber(vm, args[1], "rand()", &high)) return nilValue();
  }
  low = std::floor(low);
  high = std::floor(high);
  if (high <= low) {
    return vm.fail("rand() needs a range with something in it, got %s to %s.",
                   valueToString(numberValue(low)).c_str(),
                   valueToString(numberValue(high)).c_str());
  }
  double span = high - low;
  double pick = std::floor(
      std::uniform_real_distribution<double>(0.0, span)(generator()));
  if (pick >= span) pick = span - 1;  // the open end, in case of rounding
  return numberValue(low + pick);
}

// Fixes the sequence, so a run that uses randomness can be repeated.
Value nativeRandSeed(VM& vm, int, Value* args) {
  double seed;
  if (!wantNumber(vm, args[0], "rand_seed()", &seed)) return nilValue();
  generator().seed((uint64_t)(int64_t)seed);
  return nilValue();
}

Value nativeRound(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "round()", &value)) return nilValue();
  // Halves go away from zero, which is what people expect when they are
  // rounding a price or a score.
  return numberValue(std::round(value));
}

Value nativeSign(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "sign()", &value)) return nilValue();
  if (value > 0) return numberValue(1);
  if (value < 0) return numberValue(-1);
  return numberValue(value);  // keeps 0, -0 and nan as themselves
}

Value nativeExp(VM& vm, int, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "exp()", &value)) return nilValue();
  return numberValue(std::exp(value));
}

// log(x) is natural. log(x, base) is any other.
Value nativeLog(VM& vm, int argCount, Value* args) {
  double value;
  if (!wantNumber(vm, args[0], "log()", &value)) return nilValue();
  if (value < 0) {
    return vm.failAs("domain", "log() of a negative number: %s.",
                     valueToString(args[0]).c_str());
  }
  if (argCount < 2) return numberValue(std::log(value));

  double base;
  if (!wantNumber(vm, args[1], "log()", &base)) return nilValue();
  if (base <= 0 || base == 1) {
    return vm.failAs("domain", "log() needs a base above 0 and not 1, got %s.",
                     valueToString(args[1]).c_str());
  }
  if (base == 2) return numberValue(std::log2(value));
  if (base == 10) return numberValue(std::log10(value));
  return numberValue(std::log(value) / std::log(base));
}

Value nativeSin(VM& vm, int, Value* args) {
  double v;
  if (!wantNumber(vm, args[0], "sin()", &v)) return nilValue();
  return numberValue(std::sin(v));
}

Value nativeCos(VM& vm, int, Value* args) {
  double v;
  if (!wantNumber(vm, args[0], "cos()", &v)) return nilValue();
  return numberValue(std::cos(v));
}

Value nativeTan(VM& vm, int, Value* args) {
  double v;
  if (!wantNumber(vm, args[0], "tan()", &v)) return nilValue();
  return numberValue(std::tan(v));
}

Value nativeAsin(VM& vm, int, Value* args) {
  double v;
  if (!wantNumber(vm, args[0], "asin()", &v)) return nilValue();
  if (v < -1 || v > 1) {
    return vm.failAs("domain", "asin() needs a number from -1 to 1, got %s.",
                     valueToString(args[0]).c_str());
  }
  return numberValue(std::asin(v));
}

Value nativeAcos(VM& vm, int, Value* args) {
  double v;
  if (!wantNumber(vm, args[0], "acos()", &v)) return nilValue();
  if (v < -1 || v > 1) {
    return vm.failAs("domain", "acos() needs a number from -1 to 1, got %s.",
                     valueToString(args[0]).c_str());
  }
  return numberValue(std::acos(v));
}

// atan(y) takes one argument. atan(y, x) takes the quadrant into
// account, which is what turns a pair of offsets into an angle.
Value nativeAtan(VM& vm, int argCount, Value* args) {
  double y;
  if (!wantNumber(vm, args[0], "atan()", &y)) return nilValue();
  if (argCount < 2) return numberValue(std::atan(y));
  double x;
  if (!wantNumber(vm, args[1], "atan()", &x)) return nilValue();
  return numberValue(std::atan2(y, x));
}

// The length of the hypotenuse, without the overflow that squaring the
// sides by hand would cause for large values.
Value nativeHypot(VM& vm, int, Value* args) {
  double a, b;
  if (!wantNumber(vm, args[0], "hypot()", &a)) return nilValue();
  if (!wantNumber(vm, args[1], "hypot()", &b)) return nilValue();
  return numberValue(std::hypot(a, b));
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
  // Reading stdin can block, so this task parks first and the collector
  // does not have to wait for it. No heap access happens in between.
  vm.park();
  bool gotAny = false;
  while ((c = std::fgetc(stdin)) != EOF) {
    gotAny = true;
    if (c == '\n') break;
    line += (char)c;
  }
  vm.unpark();
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

// The byte at an index, as a number. Negative indexes count back from
// the end, the same way subscripting does.
Value stringCodeAt(VM& vm, int, Value* args) {
  ObjString* text = asString(args[0]);
  double rawIndex;
  if (!wantNumber(vm, args[1], "code_at()", &rawIndex)) return nilValue();
  long index = (long)rawIndex;
  if (index < 0) index += (long)text->length;
  if (index < 0 || index >= (long)text->length) {
    return vm.fail("code_at() index %ld out of range for length %zu.",
                   (long)rawIndex, text->length);
  }
  return numberValue((double)(unsigned char)text->chars[index]);
}

// Every byte as an array of numbers.
Value stringBytes(VM& vm, int, Value* args) {
  ObjString* text = asString(args[0]);
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  result->items.reserve(text->length);
  for (size_t i = 0; i < text->length; i++) {
    result->items.push_back(numberValue((double)(unsigned char)text->chars[i]));
  }
  return objValue((Obj*)result);
}

// Characters rather than bytes. A byte that does not begin a well formed
// UTF-8 sequence comes back on its own, so joining the result always
// gives the original string back, text or not.
Value stringChars(VM& vm, int, Value* args) {
  ObjString* text = asString(args[0]);
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  size_t i = 0;
  while (i < text->length) {
    uint32_t codePoint;
    size_t width = decodeUtf8(text->chars, text->length, i, &codePoint);
    result->items.push_back(
        objValue((Obj*)vm.runtime().copyString(text->chars + i, width)));
    i += width;
  }
  return objValue((Obj*)result);
}

// The same walk, reporting each character's code point instead.
Value stringCodePoints(VM& vm, int, Value* args) {
  ObjString* text = asString(args[0]);
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  size_t i = 0;
  while (i < text->length) {
    uint32_t codePoint;
    size_t width = decodeUtf8(text->chars, text->length, i, &codePoint);
    result->items.push_back(numberValue((double)codePoint));
    i += width;
  }
  return objValue((Obj*)result);
}

// How many characters, without building the array to count them.
Value stringCharLen(VM&, int, Value* args) {
  ObjString* text = asString(args[0]);
  size_t count = 0;
  size_t i = 0;
  while (i < text->length) {
    uint32_t codePoint;
    i += decodeUtf8(text->chars, text->length, i, &codePoint);
    count++;
  }
  return numberValue((double)count);
}

Value stringTrimStart(VM& vm, int, Value* args) {
  ObjString* text = asString(args[0]);
  size_t start = 0;
  while (start < text->length &&
         std::isspace((unsigned char)text->chars[start])) {
    start++;
  }
  return objValue((Obj*)vm.runtime().copyString(text->chars + start,
                                                text->length - start));
}

Value stringTrimEnd(VM& vm, int, Value* args) {
  ObjString* text = asString(args[0]);
  size_t end = text->length;
  while (end > 0 && std::isspace((unsigned char)text->chars[end - 1])) end--;
  return objValue((Obj*)vm.runtime().copyString(text->chars, end));
}

Value stringUpper(VM& vm, int, Value* args) {
  std::string text = toUpperCase(textOf(asString(args[0])));
  return objValue((Obj*)vm.runtime().copyString(text.data(), text.size()));
}

Value stringLower(VM& vm, int, Value* args) {
  std::string text = toLowerCase(textOf(asString(args[0])));
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

// Pads to a width. Anything already that wide is returned unchanged, so
// a column never collapses because one entry was long.
Value padWith(VM& vm, int argCount, Value* args, bool onLeft) {
  const char* who = onLeft ? "pad_left()" : "pad_right()";
  double width;
  if (!wantNumber(vm, args[1], who, &width)) return nilValue();

  std::string fill = " ";
  if (argCount > 2) {
    ObjString* given;
    if (!wantString(vm, args[2], who, &given)) return nilValue();
    fill.assign(given->chars, given->length);
    if (fill.size() != 1) {
      return vm.failAs("value", "%s fill must be one character, got %zu.", who,
                       fill.size());
    }
  }

  std::string text = textOf(asString(args[0]));
  if ((double)text.size() >= width) return args[0];
  std::string padding((size_t)width - text.size(), fill[0]);
  std::string result = onLeft ? padding + text : text + padding;
  return objValue((Obj*)vm.runtime().copyString(result.data(), result.size()));
}

Value stringPadLeft(VM& vm, int argCount, Value* args) {
  return padWith(vm, argCount, args, true);
}

Value stringPadRight(VM& vm, int argCount, Value* args) {
  return padWith(vm, argCount, args, false);
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

Value arrayLen(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  return numberValue((double)asArray(args[0])->items.size());
}

Value arrayPush(VM& vm, int argCount, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  ObjArray* array = asArray(args[0]);
  for (int i = 1; i < argCount; i++) array->items.push_back(args[i]);
  return args[0];
}

Value arrayPop(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  ObjArray* array = asArray(args[0]);
  if (array->items.empty()) return vm.fail("pop() on an empty array.");
  Value value = array->items.back();
  array->items.pop_back();
  return value;
}

Value arrayInsert(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
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
  ObjLock guard(vm.runtime(), args[0]);
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
  ObjLock guard(vm.runtime(), args[0]);
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

// Reads one element under the array's lock and leaves the callback and
// that element on the stack, ready to call. Returns false once the index
// has run past the end, which is looked at again every turn because
// another task may have changed the array since the last one.
//
// The lock is let go of before the callback runs. Holding an aggregate
// while Red code runs would let two tasks each walking the other's array
// wait for one another forever, and once the element is on the stack the
// collector can see it anyway.
bool takeElement(VM& vm, Value arrayValue, Value callback, size_t index,
                 Value* element) {
  ObjLock guard(vm.runtime(), arrayValue);
  ObjArray* array = asArray(arrayValue);
  if (index >= array->items.size()) return false;
  *element = array->items[index];
  vm.push(callback);
  vm.push(*element);
  return true;
}

// The same for the natives that look at an element without calling back
// into Red through a callback of their own. They can still re-enter it,
// through a class's eq() or str().
bool readElement(VM& vm, Value arrayValue, size_t index, Value* element) {
  ObjLock guard(vm.runtime(), arrayValue);
  ObjArray* array = asArray(arrayValue);
  if (index >= array->items.size()) return false;
  *element = array->items[index];
  return true;
}

Value arrayJoin(VM& vm, int argCount, Value* args) {
  std::string separator;
  if (argCount > 1) {
    ObjString* sep;
    if (!wantString(vm, args[1], "join()", &sep)) return nilValue();
    separator = textOf(sep);
  }
  std::string out;
  for (size_t i = 0;; i++) {
    Value element;
    if (!readElement(vm, args[0], i, &element)) break;
    if (i > 0) out += separator;
    // Outside the lock: a class's str() is Red code.
    out += vm.stringify(element);
  }
  return objValue((Obj*)vm.runtime().copyString(out.data(), out.size()));
}

// Compares by contents, recursively, unlike == which compares arrays and
// maps by identity. The depth limit stops a structure that contains
// itself from running away.
//
// Each container is copied under its own lock and then let go of before
// the walk goes any deeper, so only one aggregate is ever held at a
// time. Two tasks comparing the same pair of structures from opposite
// sides therefore cannot end up waiting on one another. The copy is a
// Red array because taking a contended lock can park this task, and a
// collection in that gap would not see values held only in a C++ local.
//
// None of that happens at all until a second thread exists.
bool deepEquals(Runtime& runtime, Value a, Value b, int depth) {
  if (depth > 64) return false;
  if (valuesEqual(a, b)) return true;

  if (isArray(a) && isArray(b)) {
    if (!runningInParallel()) {
      const std::vector<Value>& left = asArray(a)->items;
      const std::vector<Value>& right = asArray(b)->items;
      if (left.size() != right.size()) return false;
      for (size_t i = 0; i < left.size(); i++) {
        if (!deepEquals(runtime, left[i], right[i], depth + 1)) return false;
      }
      return true;
    }

    ObjArray* left = runtime.newArray();
    GCRoot leftRoot(runtime, (Obj*)left);
    ObjArray* right = runtime.newArray();
    GCRoot rightRoot(runtime, (Obj*)right);
    {
      ObjLock guard(runtime, a);
      left->items = asArray(a)->items;
    }
    {
      ObjLock guard(runtime, b);
      right->items = asArray(b)->items;
    }
    if (left->items.size() != right->items.size()) return false;
    for (size_t i = 0; i < left->items.size(); i++) {
      if (!deepEquals(runtime, left->items[i], right->items[i], depth + 1)) {
        return false;
      }
    }
    return true;
  }

  if (isMap(a) && isMap(b)) {
    // Both sides are read at once here rather than one at a time,
    // because a map is looked up rather than walked in step. The pair
    // form takes them in address order.
    ObjLock guard(runtime, asObj(a), asObj(b));
    ObjMap* left = asMap(a);
    ObjMap* right = asMap(b);
    if (left->entries.count() != right->entries.count()) return false;
    for (const ValueEntry& slot : left->entries.slots()) {
      if (!slot.used) continue;
      Value other;
      if (!right->entries.get(slot.key, &other)) return false;
      if (!deepEquals(runtime, slot.value, other, depth + 1)) return false;
    }
    return true;
  }

  return false;
}

Value valueEquals(VM& vm, int, Value* args) {
  return boolValue(deepEquals(vm.runtime(), args[0], args[1], 0));
}

// Searching uses the same comparison as ==, so a class with an eq() is
// found by value here too.
Value arrayContains(VM& vm, int, Value* args) {
  for (size_t i = 0;; i++) {
    Value element;
    if (!readElement(vm, args[0], i, &element)) break;
    if (vm.equal(element, args[1])) return boolValue(true);
  }
  return boolValue(false);
}

Value arrayIndexOf(VM& vm, int, Value* args) {
  for (size_t i = 0;; i++) {
    Value element;
    if (!readElement(vm, args[0], i, &element)) break;
    if (vm.equal(element, args[1])) return numberValue((double)i);
  }
  return numberValue(-1);
}

Value arrayReverse(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  ObjArray* array = asArray(args[0]);
  std::reverse(array->items.begin(), array->items.end());
  return args[0];
}

Value arrayClear(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  asArray(args[0])->items.clear();
  return args[0];
}

Value arraySort(VM& vm, int argCount, Value* args) {
  ObjArray* array = asArray(args[0]);
  if (argCount == 1) {
    // No callback, so nothing here re-enters the interpreter and the
    // lock can be held for the whole sort.
    ObjLock guard(vm.runtime(), args[0]);
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

  // With a comparator, each comparison re-enters the interpreter. The
  // copy is sorted instead, so that a comparator which changes the array
  // cannot corrupt the sort that is under way, and so that the lock is
  // never held while Red code runs.
  //
  // The copy is a Red array rather than a plain vector because a
  // collection can happen inside the comparator, and what is only in a
  // C++ local is not something the collector can see.
  ObjArray* copy = vm.runtime().newArray();
  GCRoot copyRoot(vm.runtime(), (Obj*)copy);
  {
    ObjLock guard(vm.runtime(), args[0]);
    copy->items = array->items;
  }
  bool failed = false;
  std::stable_sort(copy->items.begin(), copy->items.end(),
                   [&](Value a, Value b) {
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
  ObjLock guard(vm.runtime(), args[0]);
  array->items = copy->items;
  return args[0];
}

Value arrayMap(VM& vm, int, Value* args) {
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);

  for (size_t i = 0;; i++) {
    Value element;
    if (!takeElement(vm, args[0], args[1], i, &element)) break;
    Value mapped = nilValue();
    if (vm.callAndRun(args[1], 1, &mapped) != InterpretResult::Ok) {
      return vm.fail("map() callback failed.");
    }
    result->items.push_back(mapped);
  }
  return objValue((Obj*)result);
}

Value arrayFilter(VM& vm, int, Value* args) {
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);

  for (size_t i = 0;; i++) {
    Value element;
    if (!takeElement(vm, args[0], args[1], i, &element)) break;
    Value keep = nilValue();
    if (vm.callAndRun(args[1], 1, &keep) != InterpretResult::Ok) {
      return vm.fail("filter() callback failed.");
    }
    if (!isFalsey(keep)) result->items.push_back(element);
  }
  return objValue((Obj*)result);
}

// Stops at the first answer, which is the point of having these rather
// than filtering and looking at the length.
Value arrayAny(VM& vm, int, Value* args) {
  for (size_t i = 0;; i++) {
    Value element;
    if (!takeElement(vm, args[0], args[1], i, &element)) break;
    Value matched = nilValue();
    if (vm.callAndRun(args[1], 1, &matched) != InterpretResult::Ok) {
      return vm.fail("any() callback failed.");
    }
    if (!isFalsey(matched)) return boolValue(true);
  }
  return boolValue(false);
}

Value arrayAll(VM& vm, int, Value* args) {
  for (size_t i = 0;; i++) {
    Value element;
    if (!takeElement(vm, args[0], args[1], i, &element)) break;
    Value matched = nilValue();
    if (vm.callAndRun(args[1], 1, &matched) != InterpretResult::Ok) {
      return vm.fail("all() callback failed.");
    }
    if (isFalsey(matched)) return boolValue(false);
  }
  return boolValue(true);
}

// The first element the test accepts, or nil. find_index() reports where
// it was, so that nil can be told apart from an element that is nil.
Value arrayFind(VM& vm, int, Value* args) {
  for (size_t i = 0;; i++) {
    Value element;
    if (!takeElement(vm, args[0], args[1], i, &element)) break;
    Value matched = nilValue();
    if (vm.callAndRun(args[1], 1, &matched) != InterpretResult::Ok) {
      return vm.fail("find() callback failed.");
    }
    if (!isFalsey(matched)) return element;
  }
  return nilValue();
}

Value arrayFindIndex(VM& vm, int, Value* args) {
  for (size_t i = 0;; i++) {
    Value element;
    if (!takeElement(vm, args[0], args[1], i, &element)) break;
    Value matched = nilValue();
    if (vm.callAndRun(args[1], 1, &matched) != InterpretResult::Ok) {
      return vm.fail("find_index() callback failed.");
    }
    if (!isFalsey(matched)) return numberValue((double)i);
  }
  return numberValue(-1);
}

Value arrayReduce(VM& vm, int argCount, Value* args) {
  size_t start = 0;
  Value accumulator;
  if (argCount > 2) {
    accumulator = args[2];
  } else {
    if (!readElement(vm, args[0], 0, &accumulator)) {
      return vm.fail("reduce() on an empty array needs a starting value.");
    }
    start = 1;
  }

  for (size_t i = start;; i++) {
    Value element;
    {
      // The accumulator is only held in a C++ local between turns, so
      // it goes on the stack before anything can collect.
      ObjLock guard(vm.runtime(), args[0]);
      ObjArray* source = asArray(args[0]);
      if (i >= source->items.size()) break;
      element = source->items[i];
      vm.push(args[1]);
      vm.push(accumulator);
      vm.push(element);
    }
    Value next = nilValue();
    if (vm.callAndRun(args[1], 2, &next) != InterpretResult::Ok) {
      return vm.fail("reduce() callback failed.");
    }
    accumulator = next;
  }
  return accumulator;
}

// ---- map methods ----------------------------------------------------

Value mapLen(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  return numberValue((double)asMap(args[0])->entries.count());
}

Value mapGet(VM& vm, int argCount, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  Value value;
  if (asMap(args[0])->entries.get(args[1], &value)) return value;
  return argCount > 2 ? args[2] : nilValue();
}

Value mapSet(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  if (!isHashableKey(args[1])) {
    // Same kind as the index form, so one catch clause covers both ways
    // of writing it.
    return vm.failAs(
        "key",
        "A map key must be a string, number, boolean, nil, enum member "
        "or instance, got %s.",
        valueTypeName(args[1]));
  }
  asMap(args[0])->entries.set(args[1], args[2]);
  return args[0];
}

Value mapHas(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  Value ignored;
  return boolValue(asMap(args[0])->entries.get(args[1], &ignored));
}

Value mapRemove(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  return boolValue(asMap(args[0])->entries.remove(args[1]));
}

Value mapClear(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  asMap(args[0])->entries.clear();
  return args[0];
}

Value mapKeys(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  ObjArray* keys = vm.runtime().newArray();
  GCRoot keysRoot(vm.runtime(), (Obj*)keys);
  for (const ValueEntry& slot : asMap(args[0])->entries.slots()) {
    if (slot.used) keys->items.push_back(slot.key);
  }
  return objValue((Obj*)keys);
}

// Every entry as a two element array, so that a for-in loop can take a
// key and a value at once with a destructuring pattern.
Value mapEntries(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  Runtime& rt = vm.runtime();
  ObjArray* entries = rt.newArray();
  GCRoot entriesRoot(rt, (Obj*)entries);
  for (const ValueEntry& slot : asMap(args[0])->entries.slots()) {
    if (!slot.used) continue;
    ObjArray* pair = rt.newArray();
    GCRoot pairRoot(rt, (Obj*)pair);
    pair->items.push_back(slot.key);
    pair->items.push_back(slot.value);
    entries->items.push_back(objValue((Obj*)pair));
  }
  return objValue((Obj*)entries);
}

// ---- sets -----------------------------------------------------------

bool addToSet(VM& vm, ObjSet* set, Value item) {
  if (!isHashableKey(item)) {
    vm.failAs("key",
              "A set can hold strings, numbers, booleans, nil, enum "
              "members and instances, not %s.",
              valueTypeName(item));
    return false;
  }
  set->entries.set(item, nilValue());
  return true;
}

// set() builds an empty set. set(items) builds one from an array, a set
// or a string.
Value nativeSet(VM& vm, int argCount, Value* args) {
  Runtime& rt = vm.runtime();
  ObjSet* set = rt.newSet();
  GCRoot setRoot(rt, (Obj*)set);
  if (argCount == 0) return objValue((Obj*)set);

  Value source = args[0];
  if (isArray(source)) {
    for (Value item : asArray(source)->items) {
      if (!addToSet(vm, set, item)) return nilValue();
    }
  } else if (isSet(source)) {
    for (const ValueEntry& slot : asSet(source)->entries.slots()) {
      if (slot.used) set->entries.set(slot.key, nilValue());
    }
  } else if (isString(source)) {
    // Characters, so that set("héllo") holds five things rather than six.
    ObjString* text = asString(source);
    size_t i = 0;
    while (i < text->length) {
      uint32_t codePoint;
      size_t width = decodeUtf8(text->chars, text->length, i, &codePoint);
      set->entries.set(objValue((Obj*)rt.copyString(text->chars + i, width)),
                       nilValue());
      i += width;
    }
  } else {
    return vm.failAs("type",
                     "set() expects an array, a set or a string, got %s.",
                     valueTypeName(source));
  }
  return objValue((Obj*)set);
}

Value setAdd(VM& vm, int argCount, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  ObjSet* set = asSet(args[0]);
  for (int i = 1; i < argCount; i++) {
    if (!addToSet(vm, set, args[i])) return nilValue();
  }
  return args[0];
}

Value setRemove(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  return boolValue(asSet(args[0])->entries.remove(args[1]));
}

Value setHas(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  Value ignored;
  return boolValue(asSet(args[0])->entries.get(args[1], &ignored));
}

Value setLen(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  return numberValue((double)asSet(args[0])->entries.count());
}

Value setClear(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  asSet(args[0])->entries.clear();
  return args[0];
}

Value setItems(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
  ObjArray* items = vm.runtime().newArray();
  GCRoot itemsRoot(vm.runtime(), (Obj*)items);
  for (const ValueEntry& slot : asSet(args[0])->entries.slots()) {
    if (slot.used) items->items.push_back(slot.key);
  }
  return objValue((Obj*)items);
}

// The three combining operations all build a new set and leave both
// operands alone.
Value setCombine(VM& vm, Value* args, int mode) {
  if (!isSet(args[1])) {
    return vm.failAs("type", "Expected a set, got %s.",
                     valueTypeName(args[1]));
  }
  ObjSet* left = asSet(args[0]);
  ObjSet* right = asSet(args[1]);
  ObjSet* result = vm.runtime().newSet();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  // Nothing below re-enters the interpreter, so both sets can be held
  // for the whole of it. The pair form takes them in address order, so
  // two tasks combining the same two sets from opposite sides cannot
  // each end up holding what the other is waiting for.
  ObjLock guard(vm.runtime(), (Obj*)left, (Obj*)right);

  for (const ValueEntry& slot : left->entries.slots()) {
    if (!slot.used) continue;
    Value ignored;
    bool inRight = right->entries.get(slot.key, &ignored);
    // 0 union, 1 intersection, 2 difference.
    if (mode == 0 || (mode == 1 && inRight) || (mode == 2 && !inRight)) {
      result->entries.set(slot.key, nilValue());
    }
  }
  if (mode == 0) {
    for (const ValueEntry& slot : right->entries.slots()) {
      if (slot.used) result->entries.set(slot.key, nilValue());
    }
  }
  return objValue((Obj*)result);
}

Value setUnion(VM& vm, int, Value* args) { return setCombine(vm, args, 0); }
Value setIntersect(VM& vm, int, Value* args) { return setCombine(vm, args, 1); }
Value setDifference(VM& vm, int, Value* args) { return setCombine(vm, args, 2); }

Value setEquals(VM& vm, int, Value* args) {
  if (!isSet(args[1])) return boolValue(false);
  ObjSet* left = asSet(args[0]);
  ObjSet* right = asSet(args[1]);
  ObjLock guard(vm.runtime(), (Obj*)left, (Obj*)right);
  if (left->entries.count() != right->entries.count()) return boolValue(false);
  for (const ValueEntry& slot : left->entries.slots()) {
    if (!slot.used) continue;
    Value ignored;
    if (!right->entries.get(slot.key, &ignored)) return boolValue(false);
  }
  (void)vm;
  return boolValue(true);
}

// ---- enum methods ---------------------------------------------------

Value enumValues(VM& vm, int, Value* args) {
  ObjArray* result = vm.runtime().newArray();
  GCRoot resultRoot(vm.runtime(), (Obj*)result);
  result->items = asEnum(args[0])->ordered;
  return objValue((Obj*)result);
}

// The member with a given value, or nil. Useful when a number has come
// from outside the program, such as from a file.
Value enumFrom(VM& vm, int, Value* args) {
  double wanted;
  if (!wantNumber(vm, args[1], "from()", &wanted)) return nilValue();
  for (Value member : asEnum(args[0])->ordered) {
    if (asEnumMember(member)->value == wanted) return member;
  }
  return nilValue();
}

Value enumName(VM&, int, Value* args) {
  return objValue((Obj*)asEnum(args[0])->name);
}

Value enumLen(VM&, int, Value* args) {
  return numberValue((double)asEnum(args[0])->ordered.size());
}

Value mapValues(VM& vm, int, Value* args) {
  ObjLock guard(vm.runtime(), args[0]);
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
  defineGlobalFn(runtime, "eprint", nativeEprint, -1);
  defineGlobalFn(runtime, "ewrite", nativeEwrite, -1);
  defineGlobalFn(runtime, "clock", nativeClock, 0);
  defineGlobalFn(runtime, "type", nativeTypeName, 1);
  defineGlobalFn(runtime, "str", nativeStr, 1);
  defineGlobalFn(runtime, "repr", nativeRepr, 1);
  defineGlobalFn(runtime, "num", nativeNum, 1);
  defineGlobalFn(runtime, "int", nativeInt, 1);
  defineGlobalFn(runtime, "type_of", nativeTypeOf, 1);
  defineGlobalFn(runtime, "len", nativeLen, 1);
  defineGlobalFn(runtime, "assert", nativeAssert, -1);
  defineGlobalFn(runtime, "error", nativeError, -1);
  defineGlobalFn(runtime, "chr", nativeChr, 1);
  defineGlobalFn(runtime, "char", nativeChar, 1);
  defineGlobalFn(runtime, "set", nativeSet, -1);
  defineGlobalFn(runtime, "range", nativeRange, -1);
  defineGlobalFn(runtime, "input", nativeInput, -1);
  defineGlobalFn(runtime, "abs", nativeAbs, 1);
  defineGlobalFn(runtime, "floor", nativeFloor, 1);
  defineGlobalFn(runtime, "ceil", nativeCeil, 1);
  defineGlobalFn(runtime, "sqrt", nativeSqrt, 1);
  defineGlobalFn(runtime, "pow", nativePow, 2);
  defineGlobalFn(runtime, "min", nativeMin, -1);
  defineGlobalFn(runtime, "max", nativeMax, -1);
  defineGlobalFn(runtime, "round", nativeRound, 1);
  defineGlobalFn(runtime, "sign", nativeSign, 1);
  defineGlobalFn(runtime, "exp", nativeExp, 1);
  defineGlobalFn(runtime, "log", nativeLog, -1);
  defineGlobalFn(runtime, "sin", nativeSin, 1);
  defineGlobalFn(runtime, "cos", nativeCos, 1);
  defineGlobalFn(runtime, "tan", nativeTan, 1);
  defineGlobalFn(runtime, "asin", nativeAsin, 1);
  defineGlobalFn(runtime, "acos", nativeAcos, 1);
  defineGlobalFn(runtime, "atan", nativeAtan, -1);
  defineGlobalFn(runtime, "hypot", nativeHypot, 2);
  defineGlobalFn(runtime, "rand", nativeRand, -1);
  defineGlobalFn(runtime, "rand_seed", nativeRandSeed, 1);

  // The two constants worth having by name. They are ordinary bindings,
  // so a program that wants the letters for something else may shadow
  // them.
  defineGlobalValue(runtime, "PI", numberValue(3.14159265358979323846));
  defineGlobalValue(runtime, "E", numberValue(2.71828182845904523536));

  defineMethodFn(runtime, ObjType::String, "len", stringLen, 1);
  defineMethodFn(runtime, ObjType::String, "code_at", stringCodeAt, 2);
  defineMethodFn(runtime, ObjType::String, "bytes", stringBytes, 1);
  defineMethodFn(runtime, ObjType::String, "chars", stringChars, 1);
  defineMethodFn(runtime, ObjType::String, "code_points", stringCodePoints, 1);
  defineMethodFn(runtime, ObjType::String, "char_len", stringCharLen, 1);
  defineMethodFn(runtime, ObjType::String, "upper", stringUpper, 1);
  defineMethodFn(runtime, ObjType::String, "trim_start", stringTrimStart, 1);
  defineMethodFn(runtime, ObjType::String, "trim_end", stringTrimEnd, 1);
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
  defineMethodFn(runtime, ObjType::String, "pad_left", stringPadLeft, -1);
  defineMethodFn(runtime, ObjType::String, "pad_right", stringPadRight, -1);

  defineMethodFn(runtime, ObjType::Array, "len", arrayLen, 1);
  defineMethodFn(runtime, ObjType::Array, "push", arrayPush, -1);
  defineMethodFn(runtime, ObjType::Array, "pop", arrayPop, 1);
  defineMethodFn(runtime, ObjType::Array, "insert", arrayInsert, 3);
  defineMethodFn(runtime, ObjType::Array, "remove", arrayRemove, 2);
  defineMethodFn(runtime, ObjType::Array, "slice", arraySlice, -1);
  defineMethodFn(runtime, ObjType::Array, "join", arrayJoin, -1);
  defineMethodFn(runtime, ObjType::Array, "equals", valueEquals, 2);
  defineMethodFn(runtime, ObjType::Array, "contains", arrayContains, 2);
  defineMethodFn(runtime, ObjType::Array, "index_of", arrayIndexOf, 2);
  defineMethodFn(runtime, ObjType::Array, "reverse", arrayReverse, 1);
  defineMethodFn(runtime, ObjType::Array, "clear", arrayClear, 1);
  defineMethodFn(runtime, ObjType::Array, "sort", arraySort, -1);
  defineMethodFn(runtime, ObjType::Array, "map", arrayMap, 2);
  defineMethodFn(runtime, ObjType::Array, "filter", arrayFilter, 2);
  defineMethodFn(runtime, ObjType::Array, "any", arrayAny, 2);
  defineMethodFn(runtime, ObjType::Array, "all", arrayAll, 2);
  defineMethodFn(runtime, ObjType::Array, "find", arrayFind, 2);
  defineMethodFn(runtime, ObjType::Array, "find_index", arrayFindIndex, 2);
  defineMethodFn(runtime, ObjType::Array, "reduce", arrayReduce, -1);

  defineMethodFn(runtime, ObjType::Map, "len", mapLen, 1);
  defineMethodFn(runtime, ObjType::Map, "get", mapGet, -1);
  defineMethodFn(runtime, ObjType::Map, "set", mapSet, 3);
  defineMethodFn(runtime, ObjType::Map, "has", mapHas, 2);
  defineMethodFn(runtime, ObjType::Map, "remove", mapRemove, 2);
  defineMethodFn(runtime, ObjType::Map, "clear", mapClear, 1);
  defineMethodFn(runtime, ObjType::Map, "keys", mapKeys, 1);
  defineMethodFn(runtime, ObjType::Map, "values", mapValues, 1);
  defineMethodFn(runtime, ObjType::Map, "entries", mapEntries, 1);
  defineMethodFn(runtime, ObjType::Map, "equals", valueEquals, 2);

  defineMethodFn(runtime, ObjType::Set, "add", setAdd, -1);
  defineMethodFn(runtime, ObjType::Set, "remove", setRemove, 2);
  defineMethodFn(runtime, ObjType::Set, "has", setHas, 2);
  defineMethodFn(runtime, ObjType::Set, "len", setLen, 1);
  defineMethodFn(runtime, ObjType::Set, "clear", setClear, 1);
  defineMethodFn(runtime, ObjType::Set, "items", setItems, 1);
  defineMethodFn(runtime, ObjType::Set, "union", setUnion, 2);
  defineMethodFn(runtime, ObjType::Set, "intersect", setIntersect, 2);
  defineMethodFn(runtime, ObjType::Set, "difference", setDifference, 2);
  defineMethodFn(runtime, ObjType::Set, "equals", setEquals, 2);

  defineMethodFn(runtime, ObjType::Enum, "values", enumValues, 1);
  defineMethodFn(runtime, ObjType::Enum, "from", enumFrom, 2);
  defineMethodFn(runtime, ObjType::Enum, "name", enumName, 1);
  defineMethodFn(runtime, ObjType::Enum, "len", enumLen, 1);
}

}  // namespace red
