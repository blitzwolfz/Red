// Regular expressions.
//
// The matcher is a Thompson simulation: every alternative is followed at
// the same time, one input character at a time, rather than one
// alternative being followed to the end and then unwound. That costs a
// little more on ordinary patterns and it means the pathological ones
// cost nothing extra. `(a+)+b` against a long run of a's finishes here
// in the time it takes to read the a's, where a backtracking engine
// takes longer than the age of the universe. A language's own library
// should not have a performance cliff in it that a value from outside
// the program can walk off.
//
// The cost is that backreferences and lookaround are not supported: they
// are what make a regular expression stop being regular, and they are
// what the backtracking is for. docs/stdlib.md says so plainly.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common.h"

namespace red {

// A span of code points, inclusive at both ends.
struct ReRange {
  uint32_t lo;
  uint32_t hi;
};

enum class ReOp : uint8_t {
  Char,             // one code point
  Any,              // any code point, or any but a newline
  Class,            // a set of ranges, possibly negated
  Match,            // the pattern is complete
  Jump,             // continue at x
  Split,            // try x first, then y
  Save,             // record this position in slot x
  Bol,              // start of the text, or of a line
  Eol,              // end of the text, or of a line
  WordBoundary,     // between a word character and something else
  NotWordBoundary,
};

struct ReInst {
  ReOp op = ReOp::Match;
  uint32_t ch = 0;
  int x = 0;
  int y = 0;
  bool negated = false;
  std::vector<ReRange> ranges;
};

struct ReMatch {
  bool matched = false;
  // Two byte offsets per group, the whole match first. A group that did
  // not take part in the match has -1 for both.
  std::vector<int> slots;
};

class Regex {
 public:
  // Flags: "i" to ignore case (ASCII letters only), "m" to let ^ and $
  // meet line breaks, "s" to let . meet a newline.
  bool compile(const std::string& pattern, const std::string& flags,
               std::string* error);

  // Finds the leftmost match at or after `start`, which is a byte
  // offset. Where two matches start in the same place the one the
  // pattern prefers wins, which is what makes greedy and lazy operators
  // mean anything.
  ReMatch search(const std::string& text, size_t start) const;

  int groupCount() const { return groupCount_; }
  const std::string& pattern() const { return pattern_; }
  const std::string& flags() const { return flags_; }

 private:
  std::vector<ReInst> program_;
  std::string pattern_;
  std::string flags_;
  int groupCount_ = 0;
  bool ignoreCase_ = false;
  bool multiLine_ = false;
  bool dotAll_ = false;
};

}  // namespace red
