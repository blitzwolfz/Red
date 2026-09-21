// The source formatter behind `red fmt`.
//
// It re-indents and re-spaces; it does not re-wrap. Where the author put
// a line break, a line break stays. That rules out the formatter having
// an opinion about how a long expression should be broken up, which is
// the part of formatting people disagree about, and leaves it the part
// they do not: how far a line is indented, and whether there is a space
// after a comma.
//
// It has its own lexer rather than using the compiler's, because the
// compiler throws comments away and a formatter that did the same would
// be a program that deletes comments.
#pragma once

#include <string>

namespace red {

// Formats a whole file. Returns false and fills `error` when the source
// cannot be lexed, or when the result would not lex back to the same
// tokens, which would mean a bug in here.
bool formatSource(const std::string& source, std::string* out,
                  std::string* error);

}  // namespace red
