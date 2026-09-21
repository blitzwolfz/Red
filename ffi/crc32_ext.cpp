// A Red extension written in C++.
//
// The other half of this library is lib/crc32.red, which loads this file
// when it is present and falls back to its own Red implementation when it
// is not. Together they are the worked example in docs/libraries.md.
//
// Build:
//   c++ -std=c++17 -O2 -shared -fPIC -I ffi ffi/crc32_ext.cpp -o crc32_ext.so
//   # on macOS, add: -undefined dynamic_lookup
#include <cstdint>

#include "red_ffi.hpp"

namespace {

// The usual reflected CRC-32, the one zlib and PNG use. Built once on
// first use rather than written out as a literal table.
const uint32_t* table() {
  static uint32_t entries[256];
  static bool built = false;
  if (!built) {
    for (uint32_t i = 0; i < 256; i++) {
      uint32_t value = i;
      for (int bit = 0; bit < 8; bit++) {
        value = (value & 1) ? (value >> 1) ^ 0xedb88320u : value >> 1;
      }
      entries[i] = value;
    }
    built = true;
  }
  return entries;
}

}  // namespace

RED_FUNCTION(crc32_of) {
  std::string_view text;
  if (!args.string(0, &text)) {
    return ctx.fail("crc32() expects a string");
  }

  const uint32_t* entries = table();
  uint32_t crc = 0xffffffffu;
  for (unsigned char byte : text) {
    crc = entries[(crc ^ byte) & 0xff] ^ (crc >> 8);
  }
  return red::ext::number((double)(crc ^ 0xffffffffu));
}

// Reports what this extension is, so a Red wrapper can check that it
// loaded the version it expected.
RED_FUNCTION(crc32_version) { return ctx.string("1"); }
