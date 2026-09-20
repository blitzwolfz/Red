// A chunk is one unit of compiled code: a flat byte array, the constants
// it refers to, and a table that maps each byte back to a source line.
#pragma once

#include "common.h"
#include "value.h"

namespace red {

enum OpCode : uint8_t {
  OP_CONSTANT,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,

  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,
  OP_DEFINE_GLOBAL,
  OP_GET_UPVALUE,
  OP_SET_UPVALUE,
  OP_GET_PROPERTY,
  OP_SET_PROPERTY,
  OP_GET_SUPER,

  OP_EQUAL,
  OP_NOT_EQUAL,
  OP_GREATER,
  OP_GREATER_EQUAL,
  OP_LESS,
  OP_LESS_EQUAL,

  OP_ADD,
  OP_SUBTRACT,
  OP_MULTIPLY,
  OP_DIVIDE,
  OP_MODULO,
  OP_NEGATE,
  OP_NOT,

  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_JUMP_IF_TRUE,
  OP_LOOP,

  OP_CALL,
  OP_INVOKE,
  OP_SUPER_INVOKE,
  OP_CLOSURE,
  OP_CLOSE_UPVALUE,
  OP_RETURN,

  OP_CLASS,
  OP_INHERIT,
  OP_METHOD,

  OP_ARRAY,
  OP_MAP,
  OP_GET_INDEX,
  OP_SET_INDEX,

  // Converts the value on top of the stack to a string. Emitted by string
  // interpolation so that "${x}" works for any type.
  OP_TO_STRING,

  OP_TRY_BEGIN,
  OP_TRY_END,
  OP_THROW,

  OP_SPAWN,
  OP_IMPORT,
};

// Source lines are stored as runs rather than one int per byte. Straight
// line code produces long runs, so this is usually a large saving over a
// parallel array, and the lookup cost only matters when an error is
// already being reported.
struct LineRun {
  int line;
  int count;
};

class Chunk {
 public:
  std::vector<uint8_t> code;
  std::vector<Value> constants;
  std::vector<LineRun> lines;

  void write(uint8_t byte, int line);
  // Appends a constant and returns its index. Identical constants are
  // shared, which keeps the pool small for loops that reuse literals.
  int addConstant(Value value);
  int lineAt(size_t offset) const;
};

}  // namespace red
