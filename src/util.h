// Small filesystem helpers shared by the CLI, the importer and the
// standard library.
#pragma once

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

}  // namespace red
