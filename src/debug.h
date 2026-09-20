// Bytecode printer, used by `red disasm` and by --trace.
#pragma once

#include <string>

#include "chunk.h"
#include "common.h"

namespace red {

void disassembleChunk(const Chunk& chunk, const std::string& name);
// Returns the offset of the next instruction.
size_t disassembleInstruction(const Chunk& chunk, size_t offset);

}  // namespace red
