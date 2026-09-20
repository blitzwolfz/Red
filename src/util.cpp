#include "util.h"

#include <sys/stat.h>

#include <climits>
#include <cstdio>
#include <cstdlib>

namespace red {

bool fileExists(const std::string& path) {
  struct stat info;
  return ::stat(path.c_str(), &info) == 0;
}

bool readFile(const std::string& path, std::string* out) {
  FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) return false;

  std::fseek(file, 0, SEEK_END);
  long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);
  if (size < 0) {
    std::fclose(file);
    return false;
  }

  out->resize((size_t)size);
  size_t read = size > 0 ? std::fread(&(*out)[0], 1, (size_t)size, file) : 0;
  std::fclose(file);
  out->resize(read);
  return true;
}

std::string directoryOf(const std::string& path) {
  size_t slash = path.find_last_of('/');
  if (slash == std::string::npos) return ".";
  if (slash == 0) return "/";
  return path.substr(0, slash);
}

std::string joinPath(const std::string& base, const std::string& tail) {
  if (!tail.empty() && tail[0] == '/') return tail;
  if (base.empty() || base == ".") return tail;
  if (base.back() == '/') return base + tail;
  return base + "/" + tail;
}

std::string absolutePath(const std::string& path) {
  char buffer[PATH_MAX];
  if (::realpath(path.c_str(), buffer) != nullptr) return buffer;
  return path;
}

}  // namespace red
