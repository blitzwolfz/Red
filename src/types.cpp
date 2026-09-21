#include "types.h"

#include <cctype>
#include <cmath>
#include <cstring>

#include "runtime.h"
#include "vm.h"

namespace red {

namespace {

const char* kSimpleNames[] = {
    "Any", "Nil", "Bool", "Num",  "Int",   "String", "Array",
    "Map", "Set", "Fun",  "Error", "?",    "?",
};

// Is this number one a person would call whole? Red has a single number
// type, so Int asks about the value rather than about how it is stored.
bool isWholeNumber(double value) {
  return std::isfinite(value) && value == std::trunc(value);
}

bool matchesClass(ObjClass* klass, ObjClass* wanted) {
  for (ObjClass* at = klass; at != nullptr; at = at->superclass) {
    if (at == wanted) return true;
  }
  return false;
}

// Resolves a Named type to whatever its name means in this program: a
// class, or an enum. The answer is kept, because a name binds once.
Value resolveNamed(Runtime& runtime, ObjModule* where, ObjTypeDesc* type) {
  if (type->didResolve) return type->resolved;
  Value found = nilValue();
  if (where != nullptr) where->globals.get(type->name, &found);
  if (isNil(found)) runtime.builtins.get(type->name, &found);
  type->resolved = found;
  type->didResolve = true;
  return found;
}

}  // namespace

std::string typeName(ObjTypeDesc* type) {
  if (type == nullptr) return "Any";
  switch (type->kind) {
    case TypeKind::Named:
      return std::string(type->name->chars, type->name->length);
    case TypeKind::Optional:
      return typeName(type->parts[0]) + "?";
    case TypeKind::Array:
      if (type->parts.empty()) return "Array";
      return "[" + typeName(type->parts[0]) + "]";
    case TypeKind::Map:
      if (type->parts.empty()) return "Map";
      return "{" + typeName(type->parts[0]) + ": " + typeName(type->parts[1]) +
             "}";
    case TypeKind::Set:
      if (type->parts.empty()) return "Set";
      return "Set[" + typeName(type->parts[0]) + "]";
    case TypeKind::Fun: {
      if (type->parts.empty()) return "Fun";
      std::string out = "fun(";
      for (size_t i = 0; i + 1 < type->parts.size(); i++) {
        if (i > 0) out += ", ";
        out += typeName(type->parts[i]);
      }
      out += ") -> " + typeName(type->parts.back());
      return out;
    }
    default:
      return kSimpleNames[(int)type->kind];
  }
}

bool typeMatches(Runtime& runtime, ObjModule* where, ObjTypeDesc* type,
                 Value value, std::string* reason) {
  if (type == nullptr || type->kind == TypeKind::Any) return true;

  switch (type->kind) {
    case TypeKind::Any:
      return true;

    case TypeKind::Optional: {
      if (isNil(value)) return true;
      if (typeMatches(runtime, where, type->parts[0], value, reason)) {
        return true;
      }
      // The inner type reported itself. What was written is `T?`, so
      // that is what the message should name.
      *reason = "expected " + typeName(type) + ", got " + valueTypeName(value);
      return false;
    }

    case TypeKind::Nil:
      if (isNil(value)) return true;
      break;

    case TypeKind::Bool:
      if (isBool(value)) return true;
      break;

    case TypeKind::Num:
      if (isNumber(value)) return true;
      break;

    case TypeKind::Int:
      if (isNumber(value)) {
        if (isWholeNumber(asNumber(value))) return true;
        *reason = "expected Int, got the number " + valueToString(value);
        return false;
      }
      break;

    case TypeKind::String:
      if (isString(value)) return true;
      break;

    case TypeKind::Array: {
      if (!isArray(value)) break;
      if (type->parts.empty()) return true;
      // An element type is only worth writing if it is enforced, so the
      // elements are walked. docs/language.md says what that costs.
      ObjArray* array = asArray(value);
      for (size_t i = 0; i < array->items.size(); i++) {
        if (typeMatches(runtime, where, type->parts[0], array->items[i], reason)) {
          continue;
        }
        *reason = "expected " + typeName(type) + ", but element " +
                  std::to_string(i) + " is " + valueTypeName(array->items[i]);
        return false;
      }
      return true;
    }

    case TypeKind::Map: {
      if (!isMap(value)) break;
      if (type->parts.empty()) return true;
      ObjMap* map = asMap(value);
      for (const ValueEntry& entry : map->entries.slots()) {
        if (!entry.used) continue;
        if (!typeMatches(runtime, where, type->parts[0], entry.key, reason)) {
          *reason = "expected " + typeName(type) + ", but the key " +
                    valueToDisplay(entry.key) + " is " +
                    valueTypeName(entry.key);
          return false;
        }
        if (!typeMatches(runtime, where, type->parts[1], entry.value, reason)) {
          *reason = "expected " + typeName(type) + ", but the value at " +
                    valueToDisplay(entry.key) + " is " +
                    valueTypeName(entry.value);
          return false;
        }
      }
      return true;
    }

    case TypeKind::Set: {
      if (!isSet(value)) break;
      if (type->parts.empty()) return true;
      ObjSet* set = asSet(value);
      for (const ValueEntry& entry : set->entries.slots()) {
        if (!entry.used) continue;
        if (typeMatches(runtime, where, type->parts[0], entry.key, reason)) continue;
        *reason = "expected " + typeName(type) + ", but it holds " +
                  valueTypeName(entry.key);
        return false;
      }
      return true;
    }

    case TypeKind::Fun: {
      if (!isCallable(value)) break;
      if (type->parts.empty()) return true;
      // The shape is checked as far as the value can answer for itself.
      // A closure knows its arity and what it declared; a native and a
      // class know only their arity.
      int wanted = (int)type->parts.size() - 1;
      if (isClosure(value)) {
        ObjFunction* function = asClosure(value)->function;
        if (!function->hasRest &&
            (wanted < function->arity || wanted > function->maxArity)) {
          *reason = "expected " + typeName(type) + ", but that function takes " +
                    std::to_string(function->arity) + " argument" +
                    (function->arity == 1 ? "" : "s");
          return false;
        }
        for (int i = 0; i < wanted && i < (int)function->paramTypes.size();
             i++) {
          const std::string& declared = function->paramTypes[(size_t)i];
          std::string expected = typeName(type->parts[(size_t)i]);
          // An unannotated parameter accepts anything, so it fits any
          // shape that is asked of it.
          if (declared.empty() || declared == "Any" || expected == "Any") {
            continue;
          }
          if (declared != expected) {
            *reason = "expected " + typeName(type) + ", but argument " +
                      std::to_string(i + 1) + " of that function is " +
                      declared;
            return false;
          }
        }
        std::string wantedReturn = typeName(type->parts.back());
        if (!function->returnType.empty() && wantedReturn != "Any" &&
            function->returnType != "Any" &&
            function->returnType != wantedReturn) {
          *reason = "expected " + typeName(type) + ", but that function returns " +
                    function->returnType;
          return false;
        }
      }
      return true;
    }

    case TypeKind::Error:
      if (isError(value)) return true;
      break;

    case TypeKind::Named: {
      Value bound = resolveNamed(runtime, where, type);
      if (isClass(bound)) {
        if (isInstance(value) &&
            matchesClass(asInstance(value)->klass, asClass(bound))) {
          return true;
        }
        break;
      }
      if (isEnum(bound)) {
        if (isEnumMember(value) &&
            asEnumMember(value)->parent == (ObjEnum*)asObj(bound)) {
          return true;
        }
        break;
      }
      // A name that means nothing yet. Rejecting here would make a type
      // that is spelled wrong look like a type that does not match, so
      // this reports the spelling instead.
      *reason = "there is no type called " + typeName(type);
      return false;
    }
  }

  *reason = "expected " + typeName(type) + ", got " + valueTypeName(value);
  return false;
}

bool typeIsStaticallyKnown(ObjTypeDesc* type) {
  if (type == nullptr) return false;
  if (type->kind == TypeKind::Optional) {
    return typeIsStaticallyKnown(type->parts[0]);
  }
  return type->kind != TypeKind::Named;
}

ObjTypeDesc* simpleType(Runtime& runtime, TypeKind kind) {
  return runtime.simpleTypes[(size_t)kind];
}

ObjTypeDesc* typeOf(Runtime& runtime, Value value) {
  switch (value.type) {
    case ValueType::Nil:
      return simpleType(runtime, TypeKind::Nil);
    case ValueType::Bool:
      return simpleType(runtime, TypeKind::Bool);
    case ValueType::Number:
      return simpleType(runtime, isWholeNumber(asNumber(value))
                                     ? TypeKind::Int
                                     : TypeKind::Num);
    case ValueType::Obj:
      break;
  }

  switch (asObj(value)->type) {
    case ObjType::String:
      return simpleType(runtime, TypeKind::String);
    case ObjType::Array:
      return simpleType(runtime, TypeKind::Array);
    case ObjType::Map:
      return simpleType(runtime, TypeKind::Map);
    case ObjType::Set:
      return simpleType(runtime, TypeKind::Set);
    case ObjType::Error:
      return simpleType(runtime, TypeKind::Error);
    case ObjType::Closure:
    case ObjType::Native:
    case ObjType::BoundMethod:
      return simpleType(runtime, TypeKind::Fun);
    case ObjType::Instance: {
      // The class's own name, so that `type_of(p) is Point` and a `Point`
      // annotation agree about what p is.
      ObjTypeDesc* named = runtime.newTypeDesc(TypeKind::Named,
                                               asInstance(value)->klass->name,
                                               {});
      named->resolved = objValue((Obj*)asInstance(value)->klass);
      named->didResolve = true;
      return named;
    }
    case ObjType::EnumMember: {
      ObjEnum* parent = asEnumMember(value)->parent;
      ObjTypeDesc* named =
          runtime.newTypeDesc(TypeKind::Named, parent->name, {});
      named->resolved = objValue((Obj*)parent);
      named->didResolve = true;
      return named;
    }
    default:
      // Everything else answers with the name it prints under, so that
      // type_of() is total even for the types there is no syntax for.
      return runtime.newTypeDesc(
          TypeKind::Named,
          runtime.internString(valueTypeName(value)), {});
  }
}

namespace {

// A recursive descent reader for the canonical spelling. Types are
// small, so this is a plain scan with no tokeniser behind it.
struct TypeReader {
  Runtime& runtime;
  const std::string& text;
  size_t at = 0;

  void skipSpace() {
    while (at < text.size() && text[at] == ' ') at++;
  }
  bool take(char c) {
    skipSpace();
    if (at < text.size() && text[at] == c) {
      at++;
      return true;
    }
    return false;
  }
  bool takeWord(const char* word) {
    skipSpace();
    size_t length = std::strlen(word);
    if (text.compare(at, length, word) != 0) return false;
    size_t after = at + length;
    if (after < text.size() && (std::isalnum((unsigned char)text[after]) ||
                                text[after] == '_')) {
      return false;
    }
    at = after;
    return true;
  }
  std::string word() {
    skipSpace();
    size_t start = at;
    while (at < text.size() &&
           (std::isalnum((unsigned char)text[at]) || text[at] == '_')) {
      at++;
    }
    return text.substr(start, at - start);
  }

  ObjTypeDesc* read() {
    ObjTypeDesc* inner = atom();
    if (inner == nullptr) return nullptr;
    // `T??` is `T?`, so the wrapper is only ever one deep.
    bool optional = false;
    while (take('?')) optional = true;
    if (!optional) return inner;
    if (inner->kind == TypeKind::Optional || inner->kind == TypeKind::Any) {
      return inner;
    }
    return runtime.newTypeDesc(TypeKind::Optional, nullptr, {inner});
  }

  ObjTypeDesc* atom() {
    skipSpace();
    if (at >= text.size()) return nullptr;

    if (take('[')) {
      ObjTypeDesc* element = read();
      if (element == nullptr || !take(']')) return nullptr;
      return runtime.newTypeDesc(TypeKind::Array, nullptr, {element});
    }
    if (take('{')) {
      ObjTypeDesc* key = read();
      if (key == nullptr || !take(':')) return nullptr;
      ObjTypeDesc* value = read();
      if (value == nullptr || !take('}')) return nullptr;
      return runtime.newTypeDesc(TypeKind::Map, nullptr, {key, value});
    }
    if (takeWord("fun")) {
      if (!take('(')) return nullptr;
      std::vector<ObjTypeDesc*> parts;
      if (!take(')')) {
        do {
          ObjTypeDesc* part = read();
          if (part == nullptr) return nullptr;
          parts.push_back(part);
        } while (take(','));
        if (!take(')')) return nullptr;
      }
      // The return type is always written, so the parts vector always
      // ends with it and the parameter count is one less than its size.
      if (!take('-') || !take('>')) return nullptr;
      ObjTypeDesc* result = read();
      if (result == nullptr) return nullptr;
      parts.push_back(result);
      return runtime.newTypeDesc(TypeKind::Fun, nullptr, std::move(parts));
    }

    std::string name = word();
    if (name.empty()) return nullptr;
    for (int kind = 0; kind <= (int)TypeKind::Error; kind++) {
      if (name != kSimpleNames[kind]) continue;
      if (kind == (int)TypeKind::Set && take('[')) {
        ObjTypeDesc* element = read();
        if (element == nullptr || !take(']')) return nullptr;
        return runtime.newTypeDesc(TypeKind::Set, nullptr, {element});
      }
      return simpleType(runtime, (TypeKind)kind);
    }
    return runtime.newTypeDesc(TypeKind::Named, runtime.internString(name), {});
  }
};

}  // namespace

ObjTypeDesc* parseTypeText(Runtime& runtime, const std::string& text) {
  TypeReader reader{runtime, text};
  ObjTypeDesc* type = reader.read();
  if (type == nullptr) return nullptr;
  reader.skipSpace();
  if (reader.at != text.size()) return nullptr;
  return type;
}

void installTypeNames(Runtime& runtime) {
  for (int kind = 0; kind <= (int)TypeKind::Error; kind++) {
    ObjTypeDesc* type = simpleType(runtime, (TypeKind)kind);
    runtime.builtins.set(runtime.internString(kSimpleNames[kind]),
                         objValue((Obj*)type));
  }
}

void installTypes(Runtime& runtime) {
  runtime.simpleTypes.clear();
  for (int kind = 0; kind <= (int)TypeKind::Error; kind++) {
    runtime.simpleTypes.push_back(
        runtime.newTypeDesc((TypeKind)kind, nullptr, {}));
  }
  // Named and Optional never appear without parameters, but the table is
  // indexed by TypeKind, so it has to be as long as the enum.
  runtime.simpleTypes.push_back(nullptr);
  runtime.simpleTypes.push_back(nullptr);
}

}  // namespace red
