#include "value.h"

#include <cmath>
#include <cstdio>

#include "object.h"

namespace red {

namespace {

// Depth limit for printing. Arrays and maps can hold themselves, and the
// printer is used by error paths where a stack overflow would be worse
// than a truncated message.
constexpr int kMaxPrintDepth = 8;

std::string numberToString(double value) {
  if (std::isnan(value)) return "nan";
  if (std::isinf(value)) return value > 0 ? "inf" : "-inf";
  // Whole numbers print without a fractional part, which is what users
  // expect from a language with a single number type.
  if (value == std::floor(value) && std::fabs(value) < 1e15) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    return buffer;
  }
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%.14g", value);
  return buffer;
}

std::string escapeString(const std::string& text) {
  std::string out = "\"";
  for (char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      case '\r': out += "\\r"; break;
      default: out += c;
    }
  }
  out += '"';
  return out;
}

std::string stringify(Value v, bool quoteStrings, int depth);

std::string objectString(Obj* obj, bool quoteStrings, int depth) {
  switch (obj->type) {
    case ObjType::String: {
      ObjString* s = (ObjString*)obj;
      std::string text(s->chars, s->length);
      return quoteStrings ? escapeString(text) : text;
    }
    case ObjType::Function: {
      ObjFunction* fn = (ObjFunction*)obj;
      if (fn->name == nullptr) return "<script>";
      return "<fun " + std::string(fn->name->chars, fn->name->length) + ">";
    }
    case ObjType::Native: {
      ObjNative* fn = (ObjNative*)obj;
      return "<native " + std::string(fn->name->chars, fn->name->length) + ">";
    }
    case ObjType::Closure:
      return objectString((Obj*)((ObjClosure*)obj)->function, quoteStrings, depth);
    case ObjType::Upvalue:
      return "<upvalue>";
    case ObjType::Class: {
      ObjClass* c = (ObjClass*)obj;
      return std::string(c->name->chars, c->name->length);
    }
    case ObjType::Instance: {
      ObjInstance* i = (ObjInstance*)obj;
      return std::string(i->klass->name->chars, i->klass->name->length) +
             " instance";
    }
    case ObjType::BoundMethod:
      return stringify(((ObjBoundMethod*)obj)->method, quoteStrings, depth);
    case ObjType::Array: {
      if (depth >= kMaxPrintDepth) return "[...]";
      ObjArray* a = (ObjArray*)obj;
      std::string out = "[";
      for (size_t i = 0; i < a->items.size(); i++) {
        if (i > 0) out += ", ";
        out += stringify(a->items[i], true, depth + 1);
      }
      return out + "]";
    }
    case ObjType::Map: {
      if (depth >= kMaxPrintDepth) return "{...}";
      ObjMap* m = (ObjMap*)obj;
      std::string out = "{";
      bool first = true;
      for (const ValueEntry& slot : m->entries.slots()) {
        if (!slot.used || slot.tombstone) continue;
        if (!first) out += ", ";
        first = false;
        out += stringify(slot.key, true, depth + 1);
        out += ": ";
        out += stringify(slot.value, true, depth + 1);
      }
      return out + "}";
    }
    case ObjType::Module: {
      ObjModule* m = (ObjModule*)obj;
      return "<module " + std::string(m->name->chars, m->name->length) + ">";
    }
    case ObjType::Enum: {
      ObjEnum* e = (ObjEnum*)obj;
      return "<enum " + std::string(e->name->chars, e->name->length) + ">";
    }
    case ObjType::EnumMember: {
      // Printing the enum name too is the whole point: a bare number
      // tells a reader nothing when something goes wrong.
      ObjEnumMember* m = (ObjEnumMember*)obj;
      return std::string(m->parent->name->chars, m->parent->name->length) +
             "." + std::string(m->name->chars, m->name->length);
    }
    case ObjType::Channel: {
      ObjChannel* c = (ObjChannel*)obj;
      return "<channel " + std::to_string(c->buffer.size()) + "/" +
             std::to_string(c->capacity) + ">";
    }
    case ObjType::Task:
      return ((ObjTask*)obj)->done ? "<task done>" : "<task running>";
    case ObjType::File: {
      ObjFile* f = (ObjFile*)obj;
      return "<file " + std::string(f->path->chars, f->path->length) + ">";
    }
    case ObjType::Socket: {
      ObjSocket* s = (ObjSocket*)obj;
      return "<socket " + std::to_string(s->fd) + ">";
    }
    case ObjType::NativeLib: {
      ObjNativeLib* l = (ObjNativeLib*)obj;
      return "<lib " + std::string(l->path->chars, l->path->length) + ">";
    }
    case ObjType::Error: {
      ObjError* e = (ObjError*)obj;
      return "Error: " + std::string(e->message->chars, e->message->length);
    }
  }
  return "<object>";
}

std::string stringify(Value v, bool quoteStrings, int depth) {
  switch (v.type) {
    case ValueType::Nil: return "nil";
    case ValueType::Bool: return asBool(v) ? "true" : "false";
    case ValueType::Number: return numberToString(asNumber(v));
    case ValueType::Obj: return objectString(asObj(v), quoteStrings, depth);
  }
  return "nil";
}

}  // namespace

bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case ValueType::Nil: return true;
    case ValueType::Bool: return asBool(a) == asBool(b);
    case ValueType::Number: return asNumber(a) == asNumber(b);
    case ValueType::Obj: {
      // Strings are interned, so identity is enough for them too. Every
      // other object type uses reference equality on purpose: two arrays
      // with equal contents are still two arrays.
      return asObj(a) == asObj(b);
    }
  }
  return false;
}

uint32_t hashValue(Value v) {
  switch (v.type) {
    case ValueType::Nil: return 0u;
    case ValueType::Bool: return asBool(v) ? 1u : 2u;
    case ValueType::Number: {
      double d = asNumber(v);
      // Normalise negative zero so that 0 and -0 land in the same slot.
      if (d == 0) d = 0;
      uint64_t bits;
      std::memcpy(&bits, &d, sizeof(bits));
      return (uint32_t)(bits ^ (bits >> 32));
    }
    case ValueType::Obj: {
      Obj* o = asObj(v);
      if (o->type == ObjType::String) return ((ObjString*)o)->hash;
      uint64_t bits = (uint64_t)(uintptr_t)o;
      return (uint32_t)(bits ^ (bits >> 32));
    }
  }
  return 0u;
}

std::string valueToString(Value v) { return stringify(v, false, 0); }
std::string valueToDisplay(Value v) { return stringify(v, true, 0); }

const char* valueTypeName(Value v) {
  switch (v.type) {
    case ValueType::Nil: return "nil";
    case ValueType::Bool: return "bool";
    case ValueType::Number: return "number";
    case ValueType::Obj: return objectTypeName(asObj(v));
  }
  return "nil";
}

std::string objectToString(Obj* obj, bool quoteStrings) {
  return objectString(obj, quoteStrings, 0);
}

const char* objectTypeName(Obj* obj) {
  switch (obj->type) {
    case ObjType::String: return "string";
    case ObjType::Function:
    case ObjType::Closure:
    case ObjType::Native:
    case ObjType::BoundMethod: return "function";
    case ObjType::Upvalue: return "upvalue";
    case ObjType::Class: return "class";
    case ObjType::Instance: return "instance";
    case ObjType::Array: return "array";
    case ObjType::Map: return "map";
    case ObjType::Module: return "module";
    case ObjType::Enum: return "enum";
    case ObjType::EnumMember: return "enum member";
    case ObjType::Channel: return "channel";
    case ObjType::Task: return "task";
    case ObjType::File: return "file";
    case ObjType::Socket: return "socket";
    case ObjType::NativeLib: return "lib";
    case ObjType::Error: return "error";
  }
  return "object";
}

}  // namespace red
