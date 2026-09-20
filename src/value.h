// Red's value representation.
//
// A Value is a tagged struct: an 8-bit type tag plus an 8-byte payload,
// padded to 16 bytes. The alternative was NaN boxing, which packs the same
// information into a single 8-byte double. docs/design.md explains why the
// tagged struct was chosen first.
#pragma once

#include "common.h"

namespace red {

struct Obj;

enum class ValueType : uint8_t {
  Nil,
  Bool,
  Number,
  Obj,
};

struct Value {
  ValueType type;
  union {
    bool boolean;
    double number;
    Obj* obj;
  } as;
};

inline Value nilValue() {
  Value v;
  v.type = ValueType::Nil;
  v.as.obj = nullptr;
  return v;
}

inline Value boolValue(bool b) {
  Value v;
  v.type = ValueType::Bool;
  v.as.boolean = b;
  return v;
}

inline Value numberValue(double d) {
  Value v;
  v.type = ValueType::Number;
  v.as.number = d;
  return v;
}

inline Value objValue(Obj* o) {
  Value v;
  v.type = ValueType::Obj;
  v.as.obj = o;
  return v;
}

inline bool isNil(Value v) { return v.type == ValueType::Nil; }
inline bool isBool(Value v) { return v.type == ValueType::Bool; }
inline bool isNumber(Value v) { return v.type == ValueType::Number; }
inline bool isObj(Value v) { return v.type == ValueType::Obj; }

inline bool asBool(Value v) { return v.as.boolean; }
inline double asNumber(Value v) { return v.as.number; }
inline Obj* asObj(Value v) { return v.as.obj; }

// Only nil and false are falsey. Zero and the empty string are truthy,
// which matches v1 and avoids the usual class of silent bugs.
inline bool isFalsey(Value v) {
  return isNil(v) || (isBool(v) && !asBool(v));
}

bool valuesEqual(Value a, Value b);
uint32_t hashValue(Value v);

// Human readable form, used by print and by error messages.
std::string valueToString(Value v);
// Same, but strings keep their quotes. Used by the disassembler and by
// the printed form of arrays and maps.
std::string valueToDisplay(Value v);
// Name of the runtime type, as the type() builtin reports it.
const char* valueTypeName(Value v);

}  // namespace red
