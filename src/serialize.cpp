#include "serialize.h"

#include <cstring>
#include <unordered_map>

namespace red {

namespace {

// Tags for the constant pool. Numbers are fixed: changing one changes
// the format, which is what kBytecodeVersion is for.
enum class ConstantTag : uint8_t {
  Nil = 0,
  False = 1,
  True = 2,
  Number = 3,
  String = 4,
  Function = 5,
  // A new enum, written out in full.
  Enum = 6,
  // One already written, named by its position.
  EnumReference = 7,
};

class Writer {
 public:
  explicit Writer(std::string* out) : out_(*out) {}

  void byte(uint8_t value) { out_.push_back((char)value); }

  // Fixed width, used only for the header, so that a version check keeps
  // working even if the encoding below ever changes.
  void fixed(uint32_t value) {
    byte((uint8_t)(value >> 24));
    byte((uint8_t)(value >> 16));
    byte((uint8_t)(value >> 8));
    byte((uint8_t)value);
  }

  // Seven bits per byte, low group first, with the top bit set while
  // more follow. Nearly every count in a chunk is small, so this is much
  // smaller than four bytes each.
  void word(uint32_t value) {
    while (value >= 0x80) {
      byte((uint8_t)(value | 0x80));
      value >>= 7;
    }
    byte((uint8_t)value);
  }

  void number(double value) {
    uint64_t bits;
    std::memcpy(&bits, &value, sizeof(bits));
    fixed((uint32_t)(bits >> 32));
    fixed((uint32_t)bits);
  }

  void text(const char* chars, size_t length) {
    word((uint32_t)length);
    out_.append(chars, length);
  }

  void text(const std::string& value) { text(value.data(), value.size()); }

  void raw(const std::vector<uint8_t>& bytes) {
    out_.append((const char*)bytes.data(), bytes.size());
  }

 private:
  std::string& out_;
};

class Reader {
 public:
  Reader(const std::string& bytes, size_t start)
      : bytes_(bytes), at_(start) {}

  bool failed() const { return failed_; }

  uint8_t byte() {
    if (at_ >= bytes_.size()) {
      failed_ = true;
      return 0;
    }
    return (uint8_t)bytes_[at_++];
  }

  uint32_t fixed() {
    uint32_t value = 0;
    for (int i = 0; i < 4; i++) value = (value << 8) | byte();
    return value;
  }

  uint32_t word() {
    uint32_t value = 0;
    for (int shift = 0; shift < 35; shift += 7) {
      uint8_t part = byte();
      if (failed_) return 0;
      value |= (uint32_t)(part & 0x7f) << shift;
      if ((part & 0x80) == 0) return value;
    }
    failed_ = true;
    return 0;
  }

  double number() {
    uint64_t high = fixed();
    uint64_t low = fixed();
    uint64_t bits = (high << 32) | low;
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  bool text(std::string* out) {
    uint32_t length = word();
    if (failed_ || at_ + length > bytes_.size()) {
      failed_ = true;
      return false;
    }
    out->assign(bytes_.data() + at_, length);
    at_ += length;
    return true;
  }

 private:
  const std::string& bytes_;
  size_t at_;
  bool failed_ = false;
};

// ---- writing --------------------------------------------------------

struct WriteState {
  // Enums are shared by identity, so each one is written once and then
  // referred to by position.
  std::unordered_map<const ObjEnum*, uint32_t> enums;
};

bool writeValue(Writer& writer, WriteState& state, Value value,
                std::string* reason);

bool writeFunction(Writer& writer, WriteState& state, ObjFunction* function,
                   std::string* reason) {
  if (function->name == nullptr) {
    writer.byte(0);
  } else {
    writer.byte(1);
    writer.text(function->name->chars, function->name->length);
  }

  writer.word((uint32_t)function->arity);
  writer.word((uint32_t)function->maxArity);
  writer.word((uint32_t)function->upvalueCount);
  writer.word((uint32_t)function->slotCount);
  writer.byte(function->hasRest ? 1 : 0);
  writer.text(function->returnType);

  writer.word((uint32_t)function->paramTypes.size());
  for (const std::string& type : function->paramTypes) writer.text(type);

  const Chunk& chunk = function->chunk;
  writer.word((uint32_t)chunk.code.size());
  writer.raw(chunk.code);

  writer.word((uint32_t)chunk.lines.size());
  for (const LineRun& run : chunk.lines) {
    writer.word((uint32_t)run.line);
    writer.word((uint32_t)run.count);
  }

  writer.word((uint32_t)chunk.constants.size());
  for (Value constant : chunk.constants) {
    if (!writeValue(writer, state, constant, reason)) return false;
  }
  return true;
}

bool writeValue(Writer& writer, WriteState& state, Value value,
                std::string* reason) {
  switch (value.type) {
    case ValueType::Nil:
      writer.byte((uint8_t)ConstantTag::Nil);
      return true;
    case ValueType::Bool:
      writer.byte((uint8_t)(asBool(value) ? ConstantTag::True
                                          : ConstantTag::False));
      return true;
    case ValueType::Number:
      writer.byte((uint8_t)ConstantTag::Number);
      writer.number(asNumber(value));
      return true;
    case ValueType::Obj:
      break;
  }

  Obj* object = asObj(value);
  switch (object->type) {
    case ObjType::String: {
      ObjString* text = (ObjString*)object;
      writer.byte((uint8_t)ConstantTag::String);
      writer.text(text->chars, text->length);
      return true;
    }
    case ObjType::Function:
      writer.byte((uint8_t)ConstantTag::Function);
      return writeFunction(writer, state, (ObjFunction*)object, reason);
    case ObjType::Enum: {
      ObjEnum* enumeration = (ObjEnum*)object;
      auto seen = state.enums.find(enumeration);
      if (seen != state.enums.end()) {
        writer.byte((uint8_t)ConstantTag::EnumReference);
        writer.word(seen->second);
        return true;
      }
      uint32_t index = (uint32_t)state.enums.size();
      state.enums[enumeration] = index;

      writer.byte((uint8_t)ConstantTag::Enum);
      writer.text(enumeration->name->chars, enumeration->name->length);
      writer.word((uint32_t)enumeration->ordered.size());
      for (Value member : enumeration->ordered) {
        ObjEnumMember* entry = asEnumMember(member);
        writer.text(entry->name->chars, entry->name->length);
        writer.number(entry->value);
      }
      return true;
    }
    default:
      // Only what a compiler can put in a constant pool is supported.
      // Anything else means the compiler changed without this file
      // being updated.
      *reason = std::string("cannot write a constant of type ") +
                objectTypeName(object);
      return false;
  }
}

// ---- reading --------------------------------------------------------

struct ReadState {
  Runtime* runtime = nullptr;
  ObjModule* module = nullptr;
  std::vector<ObjEnum*> enums;
};

bool readValue(Reader& reader, ReadState& state, Value* out,
               std::string* reason);

ObjFunction* readFunction(Reader& reader, ReadState& state,
                          std::string* reason) {
  Runtime& runtime = *state.runtime;
  ObjFunction* function = runtime.newFunction(state.module);
  // Rooted for the whole read, because every constant allocates.
  GCRoot functionRoot(runtime, (Obj*)function);

  if (reader.byte() == 1) {
    std::string name;
    if (!reader.text(&name)) {
      *reason = "truncated function name";
      return nullptr;
    }
    function->name = runtime.internString(name);
  }

  function->arity = (int)reader.word();
  function->maxArity = (int)reader.word();
  function->upvalueCount = (int)reader.word();
  function->slotCount = (int)reader.word();
  function->hasRest = reader.byte() == 1;
  if (!reader.text(&function->returnType)) {
    *reason = "truncated return type";
    return nullptr;
  }

  uint32_t typeCount = reader.word();
  for (uint32_t i = 0; i < typeCount && !reader.failed(); i++) {
    std::string type;
    if (!reader.text(&type)) {
      *reason = "truncated parameter type";
      return nullptr;
    }
    function->paramTypes.push_back(type);
  }

  uint32_t codeLength = reader.word();
  function->chunk.code.reserve(codeLength);
  for (uint32_t i = 0; i < codeLength && !reader.failed(); i++) {
    function->chunk.code.push_back(reader.byte());
  }
  if (reader.failed()) {
    *reason = "file ends in the middle of the instructions";
    return nullptr;
  }

  uint32_t runCount = reader.word();
  for (uint32_t i = 0; i < runCount && !reader.failed(); i++) {
    LineRun run;
    run.line = (int)reader.word();
    run.count = (int)reader.word();
    function->chunk.lines.push_back(run);
  }

  uint32_t constantCount = reader.word();
  for (uint32_t i = 0; i < constantCount && !reader.failed(); i++) {
    Value constant;
    if (!readValue(reader, state, &constant, reason)) return nullptr;
    function->chunk.constants.push_back(constant);
  }

  if (reader.failed()) {
    *reason = "file ends in the middle of a function";
    return nullptr;
  }
  return function;
}

bool readValue(Reader& reader, ReadState& state, Value* out,
               std::string* reason) {
  Runtime& runtime = *state.runtime;
  ConstantTag tag = (ConstantTag)reader.byte();
  switch (tag) {
    case ConstantTag::Nil:
      *out = nilValue();
      return true;
    case ConstantTag::False:
      *out = boolValue(false);
      return true;
    case ConstantTag::True:
      *out = boolValue(true);
      return true;
    case ConstantTag::Number:
      *out = numberValue(reader.number());
      return true;
    case ConstantTag::String: {
      std::string text;
      if (!reader.text(&text)) {
        *reason = "truncated string constant";
        return false;
      }
      // Interned, not merely copied: a string constant becomes a field
      // name, a method name or a global name, and Table compares those
      // by pointer. A name defined in one module and used in another has
      // to arrive as the same object.
      *out = objValue((Obj*)runtime.internString(text));
      return true;
    }
    case ConstantTag::Function: {
      ObjFunction* nested = readFunction(reader, state, reason);
      if (nested == nullptr) return false;
      *out = objValue((Obj*)nested);
      return true;
    }
    case ConstantTag::Enum: {
      std::string name;
      if (!reader.text(&name)) {
        *reason = "truncated enum name";
        return false;
      }
      ObjEnum* enumeration = runtime.newEnum(runtime.internString(name));
      GCRoot enumRoot(runtime, (Obj*)enumeration);
      state.enums.push_back(enumeration);

      uint32_t memberCount = reader.word();
      for (uint32_t i = 0; i < memberCount && !reader.failed(); i++) {
        std::string memberName;
        if (!reader.text(&memberName)) {
          *reason = "truncated enum member";
          return false;
        }
        double value = reader.number();
        ObjString* interned = runtime.internString(memberName);
        ObjEnumMember* member =
            runtime.newEnumMember(enumeration, interned, value);
        enumeration->members.set(interned, objValue((Obj*)member));
        enumeration->ordered.push_back(objValue((Obj*)member));
      }
      if (reader.failed()) {
        *reason = "file ends in the middle of an enum";
        return false;
      }
      *out = objValue((Obj*)enumeration);
      return true;
    }
    case ConstantTag::EnumReference: {
      uint32_t index = reader.word();
      if (index >= state.enums.size()) {
        *reason = "enum reference points outside the file";
        return false;
      }
      *out = objValue((Obj*)state.enums[index]);
      return true;
    }
  }
  *reason = "unknown constant tag";
  return false;
}

}  // namespace

bool looksCompiled(const std::string& bytes) {
  return bytes.size() >= sizeof(kCompiledMagic) &&
         std::memcmp(bytes.data(), kCompiledMagic, sizeof(kCompiledMagic)) == 0;
}

bool writeCompiled(ObjFunction* root, std::string* out, std::string* reason) {
  out->clear();
  out->append(kCompiledMagic, sizeof(kCompiledMagic));

  Writer writer(out);
  writer.fixed((uint32_t)kBytecodeVersion);

  WriteState state;
  return writeFunction(writer, state, root, reason);
}

ObjFunction* readCompiled(Runtime& runtime, const std::string& bytes,
                          ObjModule* module, std::string* reason) {
  if (!looksCompiled(bytes)) {
    *reason = "not a compiled Red file";
    return nullptr;
  }

  Reader reader(bytes, sizeof(kCompiledMagic));
  uint32_t version = reader.fixed();
  if ((int)version != kBytecodeVersion) {
    *reason = "built for bytecode version " + std::to_string(version) +
              ", this build reads version " + std::to_string(kBytecodeVersion) +
              ". Compile it again from source.";
    return nullptr;
  }

  ReadState state;
  state.runtime = &runtime;
  state.module = module;
  return readFunction(reader, state, reason);
}

}  // namespace red
