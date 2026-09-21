// Small filesystem helpers shared by the CLI, the importer and the
// standard library.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace red {

// Is there a file or directory at this path?
bool fileExists(const std::string& path);
// Reads a whole file. Returns false when the file cannot be opened.
bool readFile(const std::string& path, std::string* out);
// Directory part of a path, without the trailing separator. Returns "."
// when the path has no directory part.
std::string directoryOf(const std::string& path);
// Joins two path parts. An absolute second part wins.
std::string joinPath(const std::string& base, const std::string& tail);
// Resolves ".", ".." and symlinks. Falls back to the input when the path
// does not exist yet.
std::string absolutePath(const std::string& path);

// ---------------------------------------------------------------------
// UTF-8
//
// A Red string is a sequence of bytes and may hold anything, including
// data that is not text at all. These two are for the places that want
// to read it as text: chars(), code_points(), for-in and the \u escape.

// Highest code point Unicode defines.
constexpr uint32_t kMaxCodePoint = 0x10ffff;

// Decodes the sequence starting at `index` and writes its code point.
// Returns how many bytes it used, which is always at least one.
//
// A byte that does not begin a well formed sequence is reported as
// itself and consumes one byte. Nothing is ever rejected, so walking a
// string by characters and joining the result gives back exactly what
// was there, whether or not it was valid UTF-8.
size_t decodeUtf8(const char* chars, size_t length, size_t index,
                  uint32_t* codePoint);

// Writes the UTF-8 form of a code point into `out`, which needs room for
// four bytes. Returns how many were written, or 0 when the code point is
// not one that can be encoded: above kMaxCodePoint, or a surrogate.
size_t encodeUtf8(uint32_t codePoint, char* out);

}  // namespace red
