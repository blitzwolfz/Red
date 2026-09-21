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

void Chunk::truncate(size_t newSize) {
  if (newSize >= code.size()) return;
  size_t removing = code.size() - newSize;
  while (removing > 0 && !lines.empty()) {
    LineRun& last = lines.back();
    if ((size_t)last.count > removing) {
      last.count -= (int)removing;
      removing = 0;
    } else {
      removing -= (size_t)last.count;
      lines.pop_back();
    }
  }
  code.resize(newSize);
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
