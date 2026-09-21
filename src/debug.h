// Bytecode printer, used by `red disasm` and by --trace.
#pragma once

#include <string>

#include "chunk.h"
#include "common.h"

namespace red {

void disassembleChunk(const Chunk& chunk, const std::string& name);
// Returns the offset of the next instruction.
size_t disassembleInstruction(const Chunk& chunk, size_t offset);

// How many bytes the instruction at `offset` takes, operands included.
// It lives beside the disassembler because the two have to agree about
// operand widths, and a new opcode that teaches one and not the other
// would be an easy mistake to make if they were apart.
size_t instructionLength(const Chunk& chunk, size_t offset);

}  // namespace red
