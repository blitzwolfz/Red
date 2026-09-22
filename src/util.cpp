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

bool isDirectory(const std::string& path) {
  struct stat info;
  if (::stat(path.c_str(), &info) != 0) return false;
  return S_ISDIR(info.st_mode);
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

namespace {

// Is this a continuation byte, 10xxxxxx?
bool isContinuation(unsigned char byte) { return (byte & 0xc0) == 0x80; }

// The halves of a surrogate pair. They only mean anything in UTF-16 and
// are not valid on their own in UTF-8.
bool isSurrogate(uint32_t codePoint) {
  return codePoint >= 0xd800 && codePoint <= 0xdfff;
}

}  // namespace

size_t decodeUtf8(const char* chars, size_t length, size_t index,
                  uint32_t* codePoint) {
  unsigned char first = (unsigned char)chars[index];

  // One byte: plain ASCII, which is the overwhelming majority of what
  // any program actually holds.
  if (first < 0x80) {
    *codePoint = first;
    return 1;
  }

  size_t extra;
  uint32_t value;
  uint32_t lowest;  // smallest code point this length is allowed to carry
  if ((first & 0xe0) == 0xc0) {
    extra = 1;
    value = first & 0x1f;
    lowest = 0x80;
  } else if ((first & 0xf0) == 0xe0) {
    extra = 2;
    value = first & 0x0f;
    lowest = 0x800;
  } else if ((first & 0xf8) == 0xf0) {
    extra = 3;
    value = first & 0x07;
    lowest = 0x10000;
  } else {
    // A continuation byte with nothing in front of it, or one of the
    // five and six byte forms that UTF-8 no longer has.
    *codePoint = first;
    return 1;
  }

  // The last byte of the sequence has to be inside the string. A
  // truncated one at the end is the leading byte on its own.
  if (index + extra >= length) {
    *codePoint = first;
    return 1;
  }
  for (size_t i = 1; i <= extra; i++) {
    unsigned char next = (unsigned char)chars[index + i];
    if (!isContinuation(next)) {
      *codePoint = first;
      return 1;
    }
    value = (value << 6) | (next & 0x3f);
  }

  // An overlong form encodes a small code point in more bytes than it
  // needs, which is how a check on the encoded bytes can be walked past.
  // Both it and a surrogate are treated as the single bad byte they
  // start with.
  if (value < lowest || value > kMaxCodePoint || isSurrogate(value)) {
    *codePoint = first;
    return 1;
  }

  *codePoint = value;
  return extra + 1;
}

size_t encodeUtf8(uint32_t codePoint, char* out) {
  if (codePoint > kMaxCodePoint || isSurrogate(codePoint)) return 0;

  if (codePoint < 0x80) {
    out[0] = (char)codePoint;
    return 1;
  }
  if (codePoint < 0x800) {
    out[0] = (char)(0xc0 | (codePoint >> 6));
    out[1] = (char)(0x80 | (codePoint & 0x3f));
    return 2;
  }
  if (codePoint < 0x10000) {
    out[0] = (char)(0xe0 | (codePoint >> 12));
    out[1] = (char)(0x80 | ((codePoint >> 6) & 0x3f));
    out[2] = (char)(0x80 | (codePoint & 0x3f));
    return 3;
  }
  out[0] = (char)(0xf0 | (codePoint >> 18));
  out[1] = (char)(0x80 | ((codePoint >> 12) & 0x3f));
  out[2] = (char)(0x80 | ((codePoint >> 6) & 0x3f));
  out[3] = (char)(0x80 | (codePoint & 0x3f));
  return 4;
}

}  // namespace red
