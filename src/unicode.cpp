#include "unicode.h"

#include <cstdint>

#include "unicode_case.h"
#include "util.h"

namespace red {

namespace {

// The code points whose upper case is more than one code point. There is
// no equivalent list going the other way: every simple lower case
// mapping is one to one.
struct Expansion {
  uint32_t from;
  const char* to;
};

constexpr Expansion kUpperExpansions[] = {
    {0x00df, "SS"},        // sharp s
    {0x0149, "\xca\xbcN"},  // n preceded by apostrophe
    {0x01f0, "J\xcc\x8c"},
    {0x0587, "\xd4\xb5\xd5\x92"},
    {0x1e96, "H\xcc\xb1"},
    {0x1e97, "T\xcc\x88"},
    {0x1e98, "W\xcc\x8a"},
    {0x1e99, "Y\xcc\x8a"},
    {0x1e9a, "A\xca\xbe"},
    {0x1e9e, "SS"},
    {0xfb00, "FF"},
    {0xfb01, "FI"},
    {0xfb02, "FL"},
    {0xfb03, "FFI"},
    {0xfb04, "FFL"},
    {0xfb05, "ST"},
    {0xfb06, "ST"},
    {0xfb13, "\xd5\x84\xd5\x86"},
    {0xfb14, "\xd5\x84\xd4\xb5"},
    {0xfb15, "\xd5\x84\xd4\xb1"},
    {0xfb16, "\xd5\x8e\xd5\x86"},
    {0xfb17, "\xd5\x84\xd4\xb1"},
};

// The runs are sorted, so this is a binary search over a few hundred
// entries: six or seven comparisons for a character that has a mapping,
// and the same for one that does not.
int32_t lookup(const CaseRun* runs, size_t count, uint32_t code) {
  size_t low = 0;
  size_t high = count;
  while (low < high) {
    size_t middle = (low + high) / 2;
    const CaseRun& run = runs[middle];
    if (code < run.start) {
      high = middle;
    } else if (code > run.end) {
      low = middle + 1;
    } else {
      // Inside the span, but a stride of two means only every other code
      // point in it belongs to the run.
      if (run.stride == 2 && ((code - run.start) & 1) != 0) return 0;
      return run.delta;
    }
  }
  return 0;
}

const char* upperExpansion(uint32_t code) {
  for (const Expansion& entry : kUpperExpansions) {
    if (entry.from == code) return entry.to;
    if (entry.from > code) break;
  }
  return nullptr;
}

std::string convert(const std::string& text, const CaseRun* runs, size_t count,
                    bool upper) {
  std::string out;
  out.reserve(text.size());

  size_t i = 0;
  while (i < text.size()) {
    uint32_t code;
    size_t width = decodeUtf8(text.data(), text.size(), i, &code);

    // A byte that does not begin a well formed sequence is copied as it
    // stands, so data that is not text survives being converted.
    if (width == 1 && (unsigned char)text[i] >= 0x80) {
      out += text[i];
      i += 1;
      continue;
    }

    if (upper) {
      const char* expanded = upperExpansion(code);
      if (expanded != nullptr) {
        out += expanded;
        i += width;
        continue;
      }
    }

    int32_t delta = lookup(runs, count, code);
    if (delta == 0) {
      out.append(text, i, width);
    } else {
      char buffer[4];
      size_t written = encodeUtf8((uint32_t)((int64_t)code + delta), buffer);
      if (written == 0) {
        out.append(text, i, width);
      } else {
        out.append(buffer, written);
      }
    }
    i += width;
  }
  return out;
}

}  // namespace

uint32_t simpleToUpper(uint32_t code) {
  return (uint32_t)((int64_t)code +
                    lookup(kToUpper, sizeof(kToUpper) / sizeof(CaseRun), code));
}

uint32_t simpleToLower(uint32_t code) {
  return (uint32_t)((int64_t)code +
                    lookup(kToLower, sizeof(kToLower) / sizeof(CaseRun), code));
}

std::string toUpperCase(const std::string& text) {
  return convert(text, kToUpper, sizeof(kToUpper) / sizeof(CaseRun), true);
}

std::string toLowerCase(const std::string& text) {
  return convert(text, kToLower, sizeof(kToLower) / sizeof(CaseRun), false);
}

}  // namespace red
