// Case conversion.
//
// Simple mapping only: one code point in, one out. The handful of
// characters whose case changes their length — the German sharp s, the
// ligatures, a few Greek and Armenian letters — are listed separately in
// unicode.cpp, because there are few enough to write down and leaving
// them out would be the difference between "mostly works" and "wrong".
//
// Not handled: mappings that depend on the language or on the
// surrounding letters. Turkish dotless i is the well known one, and
// Greek final sigma the other. Doing those properly needs a locale,
// which Red does not have.
#pragma once

#include <cstdint>
#include <string>

namespace red {

// The upper or lower case form of `text`, decoded as UTF-8. Bytes that
// are not valid UTF-8 pass through untouched.
std::string toUpperCase(const std::string& text);
std::string toLowerCase(const std::string& text);

// One code point's simple case, or the code point unchanged when it has
// none. Used by the regex engine's ignore-case flag, which works a
// character at a time and cannot use the expansions above.
uint32_t simpleToUpper(uint32_t code);
uint32_t simpleToLower(uint32_t code);

}  // namespace red
