#include "chunk.h"

namespace red {

void Chunk::write(uint8_t byte, int line) {
  code.push_back(byte);
  if (!lines.empty() && lines.back().line == line) {
    lines.back().count++;
  } else {
    lines.push_back({line, 1});
  }
}

int Chunk::addConstant(Value value) {
  // Constants are deduplicated. A loop body that mentions the same literal
  // on every iteration then costs one pool slot instead of one per site.
  for (size_t i = 0; i < constants.size(); i++) {
    if (valuesEqual(constants[i], value)) return (int)i;
  }
  constants.push_back(value);
  return (int)constants.size() - 1;
}

int Chunk::lineAt(size_t offset) const {
  size_t seen = 0;
  for (const LineRun& run : lines) {
    seen += (size_t)run.count;
    if (offset < seen) return run.line;
  }
  return lines.empty() ? 0 : lines.back().line;
}

}  // namespace red
