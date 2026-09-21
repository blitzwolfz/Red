#include "debug.h"

#include <cstdio>

#include "object.h"

namespace red {

namespace {

size_t simpleInstruction(const char* name, size_t offset) {
  std::printf("%s\n", name);
  return offset + 1;
}

size_t byteInstruction(const char* name, const Chunk& chunk, size_t offset) {
  uint8_t slot = chunk.code[offset + 1];
  std::printf("%-18s %4d\n", name, slot);
  return offset + 2;
}

uint16_t readShort(const Chunk& chunk, size_t offset) {
  return (uint16_t)((chunk.code[offset] << 8) | chunk.code[offset + 1]);
}

size_t shortInstruction(const char* name, const Chunk& chunk, size_t offset) {
  std::printf("%-18s %4d\n", name, readShort(chunk, offset + 1));
  return offset + 3;
}

size_t constantInstruction(const char* name, const Chunk& chunk,
                           size_t offset) {
  uint16_t constant = readShort(chunk, offset + 1);
  std::printf("%-18s %4d '%s'\n", name, constant,
              valueToDisplay(chunk.constants[constant]).c_str());
  return offset + 3;
}

size_t invokeInstruction(const char* name, const Chunk& chunk, size_t offset) {
  uint16_t constant = readShort(chunk, offset + 1);
  uint8_t argCount = chunk.code[offset + 3];
  std::printf("%-18s %4d '%s' (%d args)\n", name, constant,
              valueToDisplay(chunk.constants[constant]).c_str(), argCount);
  return offset + 4;
}

size_t jumpInstruction(const char* name, int sign, const Chunk& chunk,
                       size_t offset) {
  uint16_t jump = readShort(chunk, offset + 1);
  std::printf("%-18s %4zu -> %zu\n", name, offset,
              offset + 3 + (size_t)(sign * (int)jump));
  return offset + 3;
}

}  // namespace

size_t disassembleInstruction(const Chunk& chunk, size_t offset) {
  std::printf("%04zu ", offset);
  // Repeated line numbers print as a bar so that the eye can find the
  // boundary between statements quickly.
  if (offset > 0 && chunk.lineAt(offset) == chunk.lineAt(offset - 1)) {
    std::printf("   | ");
  } else {
    std::printf("%4d ", chunk.lineAt(offset));
  }

  uint8_t instruction = chunk.code[offset];
  switch (instruction) {
    case OP_CONSTANT: return constantInstruction("CONSTANT", chunk, offset);
    case OP_NIL: return simpleInstruction("NIL", offset);
    case OP_TRUE: return simpleInstruction("TRUE", offset);
    case OP_FALSE: return simpleInstruction("FALSE", offset);
    case OP_POP: return simpleInstruction("POP", offset);
    case OP_GET_LOCAL: return byteInstruction("GET_LOCAL", chunk, offset);
    case OP_SET_LOCAL: return byteInstruction("SET_LOCAL", chunk, offset);
    case OP_GET_GLOBAL: return constantInstruction("GET_GLOBAL", chunk, offset);
    case OP_SET_GLOBAL: return constantInstruction("SET_GLOBAL", chunk, offset);
    case OP_DEFINE_GLOBAL:
      return constantInstruction("DEFINE_GLOBAL", chunk, offset);
    case OP_GET_UPVALUE: return byteInstruction("GET_UPVALUE", chunk, offset);
    case OP_SET_UPVALUE: return byteInstruction("SET_UPVALUE", chunk, offset);
    case OP_GET_PROPERTY:
      return constantInstruction("GET_PROPERTY", chunk, offset);
    case OP_SET_PROPERTY:
      return constantInstruction("SET_PROPERTY", chunk, offset);
    case OP_GET_SUPER: return constantInstruction("GET_SUPER", chunk, offset);
    case OP_EQUAL: return simpleInstruction("EQUAL", offset);
    case OP_NOT_EQUAL: return simpleInstruction("NOT_EQUAL", offset);
    case OP_GREATER: return simpleInstruction("GREATER", offset);
    case OP_GREATER_EQUAL: return simpleInstruction("GREATER_EQUAL", offset);
    case OP_LESS: return simpleInstruction("LESS", offset);
    case OP_LESS_EQUAL: return simpleInstruction("LESS_EQUAL", offset);
    case OP_ADD: return simpleInstruction("ADD", offset);
    case OP_SUBTRACT: return simpleInstruction("SUBTRACT", offset);
    case OP_MULTIPLY: return simpleInstruction("MULTIPLY", offset);
    case OP_DIVIDE: return simpleInstruction("DIVIDE", offset);
    case OP_MODULO: return simpleInstruction("MODULO", offset);
    case OP_NEGATE: return simpleInstruction("NEGATE", offset);
    case OP_NOT: return simpleInstruction("NOT", offset);
    case OP_JUMP: return jumpInstruction("JUMP", 1, chunk, offset);
    case OP_JUMP_IF_FALSE:
      return jumpInstruction("JUMP_IF_FALSE", 1, chunk, offset);
    case OP_JUMP_IF_TRUE:
      return jumpInstruction("JUMP_IF_TRUE", 1, chunk, offset);
    case OP_LOOP: return jumpInstruction("LOOP", -1, chunk, offset);
    case OP_CALL: return byteInstruction("CALL", chunk, offset);
    case OP_INVOKE: return invokeInstruction("INVOKE", chunk, offset);
    case OP_SUPER_INVOKE: return invokeInstruction("SUPER_INVOKE", chunk, offset);
    case OP_CLOSURE: {
      size_t next = offset + 1;
      uint16_t constant = readShort(chunk, next);
      next += 2;
      std::printf("%-18s %4d '%s'\n", "CLOSURE", constant,
                  valueToDisplay(chunk.constants[constant]).c_str());
      ObjFunction* function = asFunction(chunk.constants[constant]);
      for (int i = 0; i < function->upvalueCount; i++) {
        int isLocal = chunk.code[next++];
        int index = chunk.code[next++];
        std::printf("%04zu      |                     %s %d\n", next - 2,
                    isLocal ? "local" : "upvalue", index);
      }
      return next;
    }
    case OP_CLOSE_UPVALUE: return simpleInstruction("CLOSE_UPVALUE", offset);
    case OP_RETURN: return simpleInstruction("RETURN", offset);
    case OP_CLASS: return constantInstruction("CLASS", chunk, offset);
    case OP_INHERIT: return simpleInstruction("INHERIT", offset);
    case OP_METHOD: return constantInstruction("METHOD", chunk, offset);
    case OP_ARRAY: return shortInstruction("ARRAY", chunk, offset);
    case OP_MAP: return shortInstruction("MAP", chunk, offset);
    case OP_GET_INDEX: return simpleInstruction("GET_INDEX", offset);
    case OP_SET_INDEX: return simpleInstruction("SET_INDEX", offset);
    case OP_TO_STRING: return simpleInstruction("TO_STRING", offset);
    case OP_TRY_BEGIN: return jumpInstruction("TRY_BEGIN", 1, chunk, offset);
    case OP_TRY_END: return simpleInstruction("TRY_END", offset);
    case OP_THROW: return simpleInstruction("THROW", offset);
    case OP_SPAWN: return byteInstruction("SPAWN", chunk, offset);
    case OP_IMPORT: return constantInstruction("IMPORT", chunk, offset);
    case OP_DUP: return simpleInstruction("DUP", offset);
    case OP_DUP2: return simpleInstruction("DUP2", offset);
    case OP_BIT_AND: return simpleInstruction("BIT_AND", offset);
    case OP_BIT_OR: return simpleInstruction("BIT_OR", offset);
    case OP_BIT_XOR: return simpleInstruction("BIT_XOR", offset);
    case OP_BIT_NOT: return simpleInstruction("BIT_NOT", offset);
    case OP_SHIFT_LEFT: return simpleInstruction("SHIFT_LEFT", offset);
    case OP_SHIFT_RIGHT: return simpleInstruction("SHIFT_RIGHT", offset);
    case OP_ITER_PREP: return simpleInstruction("ITER_PREP", offset);
    case OP_ITER_NEXT: {
      uint8_t sequence = chunk.code[offset + 1];
      uint8_t index = chunk.code[offset + 2];
      uint16_t jump = readShort(chunk, offset + 3);
      std::printf("%-18s seq %d idx %d -> %zu\n", "ITER_NEXT", sequence, index,
                  offset + 5 + (size_t)jump);
      return offset + 5;
    }
    case OP_CATCH_MATCHES: return simpleInstruction("CATCH_MATCHES", offset);
    case OP_DESTRUCTURE_INDEX:
      return shortInstruction("DESTRUCTURE_INDEX", chunk, offset);
    case OP_DESTRUCTURE_REST:
      return shortInstruction("DESTRUCTURE_REST", chunk, offset);
    case OP_DESTRUCTURE_FIELD:
      return constantInstruction("DESTRUCTURE_FIELD", chunk, offset);
    case OP_JUMP_IF_ARG: {
      uint8_t index = chunk.code[offset + 1];
      uint16_t jump = readShort(chunk, offset + 2);
      std::printf("%-18s arg %d -> %zu\n", "JUMP_IF_ARG", index,
                  offset + 4 + (size_t)jump);
      return offset + 4;
    }
    default:
      std::printf("Unknown opcode %d\n", instruction);
      return offset + 1;
  }
}

size_t instructionLength(const Chunk& chunk, size_t offset) {
  switch (chunk.code[offset]) {
    case OP_GET_LOCAL:
    case OP_SET_LOCAL:
    case OP_GET_UPVALUE:
    case OP_SET_UPVALUE:
    case OP_CALL:
    case OP_SPAWN:
      return 2;

    case OP_CONSTANT:
    case OP_GET_GLOBAL:
    case OP_SET_GLOBAL:
    case OP_DEFINE_GLOBAL:
    case OP_GET_PROPERTY:
    case OP_SET_PROPERTY:
    case OP_GET_SUPER:
    case OP_CLASS:
    case OP_METHOD:
    case OP_IMPORT:
    case OP_DESTRUCTURE_FIELD:
    case OP_ARRAY:
    case OP_MAP:
    case OP_DESTRUCTURE_INDEX:
    case OP_DESTRUCTURE_REST:
    case OP_JUMP:
    case OP_JUMP_IF_FALSE:
    case OP_JUMP_IF_TRUE:
    case OP_LOOP:
    case OP_TRY_BEGIN:
      return 3;

    case OP_INVOKE:
    case OP_SUPER_INVOKE:
    case OP_JUMP_IF_ARG:
      return 4;

    case OP_ITER_NEXT:
      return 5;

    case OP_CLOSURE: {
      // Two operand bytes, then a pair per upvalue.
      uint16_t constant = readShort(chunk, offset + 1);
      ObjFunction* function = asFunction(chunk.constants[constant]);
      return 3 + (size_t)function->upvalueCount * 2;
    }

    default:
      return 1;
  }
}

void disassembleChunk(const Chunk& chunk, const std::string& name) {
  std::printf("== %s ==\n", name.c_str());
  for (size_t offset = 0; offset < chunk.code.size();) {
    offset = disassembleInstruction(chunk, offset);
  }

  // Nested functions live in the constant pool. Printing them after the
  // parent keeps the output in a readable order.
  for (Value constant : chunk.constants) {
    if (isObj(constant) && asObj(constant)->type == ObjType::Function) {
      ObjFunction* function = asFunction(constant);
      std::printf("\n");
      disassembleChunk(function->chunk,
                       function->name == nullptr
                           ? "<script>"
                           : std::string(function->name->chars,
                                         function->name->length));
    }
  }
}

}  // namespace red
