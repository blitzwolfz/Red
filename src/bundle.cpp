#include "bundle.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace red {

// The archive inside the payload. Versioned separately from the .redc
// files it carries: the layout of the table around them can change
// without the bytecode format moving.
constexpr char kArchiveMagic[4] = {'R', 'E', 'D', 'X'};
constexpr unsigned char kArchiveVersion = 1;

namespace {

// The length is written little endian so that a bundle built on one
// machine is read the same way on another. Every platform Red runs on is
// little endian, but the format should not depend on that.
void writeLength(char* out, uint64_t value) {
  for (int i = 0; i < 8; i++) out[i] = (char)((value >> (i * 8)) & 0xff);
}

uint64_t readLength(const char* in) {
  uint64_t value = 0;
  for (int i = 0; i < 8; i++) {
    value |= (uint64_t)(unsigned char)in[i] << (i * 8);
  }
  return value;
}

// Length of the file with any bundle removed: what a plain interpreter
// would have been. Returns the whole length when there is no trailer.
bool baseLength(FILE* file, long total, long* out) {
  *out = total;
  if (total < (long)kBundleTrailerSize) return true;

  char trailer[kBundleTrailerSize];
  if (std::fseek(file, total - (long)kBundleTrailerSize, SEEK_SET) != 0) {
    return false;
  }
  if (std::fread(trailer, 1, sizeof(trailer), file) != sizeof(trailer)) {
    return false;
  }
  if (std::memcmp(trailer + 8, kBundleMagic, sizeof(kBundleMagic)) != 0) {
    return true;
  }

  uint64_t length = readLength(trailer);
  // A trailer that claims more than the file holds is damage, not a
  // bundle. Treat the file as a plain interpreter rather than reading
  // outside it.
  if (length > (uint64_t)(total - (long)kBundleTrailerSize)) return true;
  *out = total - (long)kBundleTrailerSize - (long)length;
  return true;
}

// Numbers in the archive are varints, the same encoding the .redc format
// uses, so that a table of short strings does not pay for eight byte
// lengths.
void writeVarint(std::string* out, uint64_t value) {
  while (value >= 0x80) {
    out->push_back((char)((value & 0x7f) | 0x80));
    value >>= 7;
  }
  out->push_back((char)value);
}

bool readVarint(const std::string& in, size_t* at, uint64_t* out) {
  uint64_t value = 0;
  int shift = 0;
  while (true) {
    if (*at >= in.size() || shift > 63) return false;
    unsigned char byte = (unsigned char)in[(*at)++];
    value |= (uint64_t)(byte & 0x7f) << shift;
    if ((byte & 0x80) == 0) break;
    shift += 7;
  }
  *out = value;
  return true;
}

void writeText(std::string* out, const std::string& text) {
  writeVarint(out, text.size());
  out->append(text);
}

bool readText(const std::string& in, size_t* at, std::string* out) {
  uint64_t length = 0;
  if (!readVarint(in, at, &length)) return false;
  if (length > in.size() - *at) return false;
  out->assign(in, *at, (size_t)length);
  *at += (size_t)length;
  return true;
}

}  // namespace

const BundledModule* Bundle::find(const std::string& path) const {
  for (const BundledModule& module : modules) {
    if (module.path == path) return &module;
  }
  return nullptr;
}

std::string Bundle::resolve(const std::string& from,
                            const std::string& request) const {
  const BundledModule* module = find(from);
  if (module == nullptr) return "";
  for (const auto& link : module->links) {
    if (link.first == request) return link.second;
  }
  return "";
}

std::string encodeBundle(const Bundle& bundle) {
  std::string out;
  out.append(kArchiveMagic, sizeof(kArchiveMagic));
  out.push_back((char)kArchiveVersion);
  writeVarint(&out, bundle.modules.size());
  for (const BundledModule& module : bundle.modules) {
    writeText(&out, module.path);
    writeText(&out, module.name);
    writeVarint(&out, module.links.size());
    for (const auto& link : module.links) {
      writeText(&out, link.first);
      writeText(&out, link.second);
    }
    writeText(&out, module.code);
  }
  writeVarint(&out, bundle.entry);
  return out;
}

bool decodeBundle(const std::string& bytes, Bundle* out, std::string* reason) {
  size_t at = 0;
  if (bytes.size() < sizeof(kArchiveMagic) + 1 ||
      std::memcmp(bytes.data(), kArchiveMagic, sizeof(kArchiveMagic)) != 0) {
    *reason = "not a bundled program";
    return false;
  }
  at = sizeof(kArchiveMagic);
  unsigned char version = (unsigned char)bytes[at++];
  if (version != kArchiveVersion) {
    *reason = "built by a different version of Red";
    return false;
  }

  uint64_t count = 0;
  if (!readVarint(bytes, &at, &count)) {
    *reason = "truncated";
    return false;
  }
  out->modules.resize((size_t)count);
  for (BundledModule& module : out->modules) {
    uint64_t links = 0;
    if (!readText(bytes, &at, &module.path) ||
        !readText(bytes, &at, &module.name) ||
        !readVarint(bytes, &at, &links)) {
      *reason = "truncated";
      return false;
    }
    module.links.resize((size_t)links);
    for (auto& link : module.links) {
      if (!readText(bytes, &at, &link.first) ||
          !readText(bytes, &at, &link.second)) {
        *reason = "truncated";
        return false;
      }
    }
    if (!readText(bytes, &at, &module.code)) {
      *reason = "truncated";
      return false;
    }
  }

  uint64_t entry = 0;
  if (!readVarint(bytes, &at, &entry) || entry >= out->modules.size()) {
    *reason = "no entry point";
    return false;
  }
  out->entry = (size_t)entry;
  return true;
}

bool readBundle(const std::string& executable, std::string* payload) {
  FILE* file = std::fopen(executable.c_str(), "rb");
  if (file == nullptr) return false;

  bool found = false;
  if (std::fseek(file, 0, SEEK_END) == 0) {
    long total = std::ftell(file);
    long base = total;
    if (total > (long)kBundleTrailerSize && baseLength(file, total, &base) &&
        base != total) {
      long length = total - (long)kBundleTrailerSize - base;
      payload->resize((size_t)length);
      if (std::fseek(file, base, SEEK_SET) == 0 &&
          std::fread(payload->data(), 1, (size_t)length, file) ==
              (size_t)length) {
        found = true;
      } else {
        payload->clear();
      }
    }
  }
  std::fclose(file);
  return found;
}

bool writeBundle(const std::string& executable, const std::string& payload,
                 const std::string& outPath, std::string* reason) {
  FILE* in = std::fopen(executable.c_str(), "rb");
  if (in == nullptr) {
    *reason = "cannot read the interpreter at '" + executable + "'";
    return false;
  }
  if (std::fseek(in, 0, SEEK_END) != 0) {
    std::fclose(in);
    *reason = "cannot measure '" + executable + "'";
    return false;
  }
  long total = std::ftell(in);
  long base = total;
  if (!baseLength(in, total, &base)) {
    std::fclose(in);
    *reason = "cannot read '" + executable + "'";
    return false;
  }

  std::string image;
  image.resize((size_t)base);
  if (std::fseek(in, 0, SEEK_SET) != 0 ||
      std::fread(image.data(), 1, (size_t)base, in) != (size_t)base) {
    std::fclose(in);
    *reason = "cannot read '" + executable + "'";
    return false;
  }
  std::fclose(in);

  // Written to a temporary name and moved into place, so that building
  // over a program that is currently running replaces the directory
  // entry instead of writing through to the running image.
  std::string temporary = outPath + ".tmp";
  FILE* out = std::fopen(temporary.c_str(), "wb");
  if (out == nullptr) {
    *reason = "cannot write '" + outPath + "'";
    return false;
  }

  char trailer[kBundleTrailerSize];
  writeLength(trailer, (uint64_t)payload.size());
  std::memcpy(trailer + 8, kBundleMagic, sizeof(kBundleMagic));

  bool ok = std::fwrite(image.data(), 1, image.size(), out) == image.size() &&
            std::fwrite(payload.data(), 1, payload.size(), out) ==
                payload.size() &&
            std::fwrite(trailer, 1, sizeof(trailer), out) == sizeof(trailer);
  ok = (std::fclose(out) == 0) && ok;
  if (!ok) {
    std::remove(temporary.c_str());
    *reason = "cannot write '" + outPath + "'";
    return false;
  }

  if (::chmod(temporary.c_str(), 0755) != 0 ||
      std::rename(temporary.c_str(), outPath.c_str()) != 0) {
    std::remove(temporary.c_str());
    *reason = "cannot finish writing '" + outPath + "'";
    return false;
  }
  return true;
}

std::string selfExecutablePath(const std::string& fallback) {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  if (size > 0) {
    std::vector<char> buffer(size + 1, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
      return std::string(buffer.data());
    }
  }
#elif defined(__linux__)
  std::vector<char> buffer(4096, '\0');
  ssize_t used = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
  if (used > 0) return std::string(buffer.data(), (size_t)used);
#endif
  return fallback;
}

}  // namespace red
