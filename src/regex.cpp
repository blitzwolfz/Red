#include "regex.h"

#include <algorithm>

#include "unicode.h"
#include "util.h"

namespace red {

namespace {

// A pattern that reaches either of these is refused rather than allowed
// to turn into a memory problem.
constexpr int kMaxProgram = 100000;
constexpr int kMaxRepeat = 1000;

bool isWordCodePoint(uint32_t c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') || c == '_';
}

// The other case of a letter, or the code point unchanged. This is the
// simple mapping, so it agrees with upper() and lower() on everything
// except the few characters whose case changes their length, which a
// character-at-a-time matcher cannot represent anyway.
uint32_t swapCase(uint32_t c) {
  uint32_t other = simpleToUpper(c);
  if (other != c) return other;
  return simpleToLower(c);
}

// Targets are indexes into the fragment that holds them, so joining two
// fragments means moving one of them along.
void shiftTargets(std::vector<ReInst>& fragment, int by) {
  for (ReInst& inst : fragment) {
    if (inst.op == ReOp::Jump) {
      inst.x += by;
    } else if (inst.op == ReOp::Split) {
      inst.x += by;
      inst.y += by;
    }
    // Save's x is a slot number, not a target, so it is left alone.
  }
}

void append(std::vector<ReInst>* into, std::vector<ReInst> tail) {
  shiftTargets(tail, (int)into->size());
  into->insert(into->end(), tail.begin(), tail.end());
}

// The ranges behind \d, \w and \s, and their negations.
void shorthandRanges(char which, std::vector<ReRange>* out) {
  switch (which) {
    case 'd':
      out->push_back({'0', '9'});
      break;
    case 'w':
      out->push_back({'0', '9'});
      out->push_back({'A', 'Z'});
      out->push_back({'_', '_'});
      out->push_back({'a', 'z'});
      break;
    case 's':
      out->push_back({'\t', '\r'});  // tab, newline, vertical tab, form feed, return
      out->push_back({' ', ' '});
      break;
    default:
      break;
  }
}

// Everything the given ranges leave out. Used for \D, \W and \S inside a
// character class, where the class itself may not be negated.
std::vector<ReRange> complement(std::vector<ReRange> ranges) {
  std::sort(ranges.begin(), ranges.end(),
            [](const ReRange& a, const ReRange& b) { return a.lo < b.lo; });
  std::vector<ReRange> out;
  uint32_t next = 0;
  for (const ReRange& range : ranges) {
    if (range.lo > next) out.push_back({next, range.lo - 1});
    if (range.hi + 1 > next) next = range.hi + 1;
  }
  if (next <= kMaxCodePoint) out.push_back({next, kMaxCodePoint});
  return out;
}

class Parser {
 public:
  Parser(const std::string& pattern, bool ignoreCase)
      : pattern_(pattern), ignoreCase_(ignoreCase) {}

  bool parse(std::vector<ReInst>* out, std::string* error) {
    if (!alternate(out, error)) return false;
    if (at_ < pattern_.size()) {
      *error = "unexpected '" + pattern_.substr(at_, 1) + "'";
      return false;
    }
    return true;
  }

  int groups() const { return groups_; }

 private:
  const std::string& pattern_;
  size_t at_ = 0;
  int groups_ = 0;
  bool ignoreCase_;

  bool atEnd() const { return at_ >= pattern_.size(); }
  char peek() const { return atEnd() ? '\0' : pattern_[at_]; }
  char advance() { return pattern_[at_++]; }
  bool match(char c) {
    if (peek() != c) return false;
    at_++;
    return true;
  }

  bool tooBig(const std::vector<ReInst>& fragment, std::string* error) {
    if ((int)fragment.size() <= kMaxProgram) return false;
    *error = "pattern is too large";
    return true;
  }

  // alternate -> concat ( "|" concat )*
  bool alternate(std::vector<ReInst>* out, std::string* error) {
    std::vector<ReInst> left;
    if (!concat(&left, error)) return false;

    while (match('|')) {
      std::vector<ReInst> right;
      if (!concat(&right, error)) return false;

      std::vector<ReInst> joined;
      ReInst split;
      split.op = ReOp::Split;
      split.x = 1;
      split.y = (int)left.size() + 2;
      joined.push_back(split);

      shiftTargets(left, 1);
      joined.insert(joined.end(), left.begin(), left.end());

      ReInst jump;
      jump.op = ReOp::Jump;
      jump.x = (int)left.size() + (int)right.size() + 2;
      joined.push_back(jump);

      shiftTargets(right, (int)joined.size());
      joined.insert(joined.end(), right.begin(), right.end());

      left = joined;
      if (tooBig(left, error)) return false;
    }
    *out = left;
    return true;
  }

  // concat -> repeat*
  bool concat(std::vector<ReInst>* out, std::string* error) {
    out->clear();
    while (!atEnd() && peek() != '|' && peek() != ')') {
      std::vector<ReInst> piece;
      if (!repeat(&piece, error)) return false;
      append(out, piece);
      if (tooBig(*out, error)) return false;
    }
    return true;
  }

  // repeat -> atom ( "*" | "+" | "?" | "{" n ( "," m? )? "}" ) "?"?
  bool repeat(std::vector<ReInst>* out, std::string* error) {
    std::vector<ReInst> body;
    if (!atom(&body, error)) return false;

    for (;;) {
      if (match('*')) {
        star(&body, !match('?'));
      } else if (match('+')) {
        plus(&body, !match('?'));
      } else if (match('?')) {
        optional(&body, !match('?'));
      } else if (peek() == '{' && looksLikeCount()) {
        if (!counted(&body, error)) return false;
      } else {
        break;
      }
      if (tooBig(body, error)) return false;
    }
    *out = body;
    return true;
  }

  // A '{' only starts a count when digits and a '}' follow. Otherwise it
  // is an ordinary character, which is what most patterns that contain
  // one mean by it.
  bool looksLikeCount() const {
    size_t i = at_ + 1;
    size_t digits = 0;
    while (i < pattern_.size() && pattern_[i] >= '0' && pattern_[i] <= '9') {
      i++;
      digits++;
    }
    if (digits == 0) return false;
    if (i < pattern_.size() && pattern_[i] == ',') {
      i++;
      while (i < pattern_.size() && pattern_[i] >= '0' && pattern_[i] <= '9') i++;
    }
    return i < pattern_.size() && pattern_[i] == '}';
  }

  void star(std::vector<ReInst>* body, bool greedy) {
    std::vector<ReInst> out;
    ReInst split;
    split.op = ReOp::Split;
    if (greedy) {
      split.x = 1;
      split.y = (int)body->size() + 2;
    } else {
      split.x = (int)body->size() + 2;
      split.y = 1;
    }
    out.push_back(split);
    shiftTargets(*body, 1);
    out.insert(out.end(), body->begin(), body->end());
    ReInst jump;
    jump.op = ReOp::Jump;
    jump.x = 0;
    out.push_back(jump);
    *body = out;
  }

  void plus(std::vector<ReInst>* body, bool greedy) {
    ReInst split;
    split.op = ReOp::Split;
    if (greedy) {
      split.x = 0;
      split.y = (int)body->size() + 1;
    } else {
      split.x = (int)body->size() + 1;
      split.y = 0;
    }
    body->push_back(split);
  }

  void optional(std::vector<ReInst>* body, bool greedy) {
    std::vector<ReInst> out;
    ReInst split;
    split.op = ReOp::Split;
    if (greedy) {
      split.x = 1;
      split.y = (int)body->size() + 1;
    } else {
      split.x = (int)body->size() + 1;
      split.y = 1;
    }
    out.push_back(split);
    shiftTargets(*body, 1);
    out.insert(out.end(), body->begin(), body->end());
    *body = out;
  }

  // A count is written out as copies, which is what keeps the matcher
  // itself simple. The limit above is what stops {1,1000000} from
  // becoming a memory problem.
  bool counted(std::vector<ReInst>* body, std::string* error) {
    advance();  // '{'
    int low = 0;
    while (peek() >= '0' && peek() <= '9') low = low * 10 + (advance() - '0');
    int high = low;
    bool open = false;
    if (match(',')) {
      if (peek() == '}') {
        open = true;
      } else {
        high = 0;
        while (peek() >= '0' && peek() <= '9') high = high * 10 + (advance() - '0');
      }
    }
    if (!match('}')) {
      *error = "expected '}' to close a repetition count";
      return false;
    }
    if (low > kMaxRepeat || high > kMaxRepeat) {
      *error = "a repetition count above 1000 is not supported";
      return false;
    }
    if (!open && high < low) {
      *error = "a repetition count counts upwards";
      return false;
    }

    const std::vector<ReInst> unit = *body;
    std::vector<ReInst> out;
    for (int i = 0; i < low; i++) {
      append(&out, unit);
      if (tooBig(out, error)) return false;
    }
    if (open) {
      std::vector<ReInst> tail = unit;
      star(&tail, true);
      append(&out, tail);
    } else {
      for (int i = low; i < high; i++) {
        std::vector<ReInst> tail = unit;
        optional(&tail, true);
        append(&out, tail);
        if (tooBig(out, error)) return false;
      }
    }
    *body = out;
    return true;
  }

  void emitChar(std::vector<ReInst>* out, uint32_t code) {
    // With the i flag a letter becomes the two-element set that it and
    // its other case make up, which keeps the matcher free of case rules.
    if (ignoreCase_ && swapCase(code) != code) {
      ReInst inst;
      inst.op = ReOp::Class;
      inst.ranges.push_back({code, code});
      uint32_t other = swapCase(code);
      inst.ranges.push_back({other, other});
      out->push_back(inst);
      return;
    }
    ReInst inst;
    inst.op = ReOp::Char;
    inst.ch = code;
    out->push_back(inst);
  }

  // atom -> "(" ( "?:" )? alternate ")" | "[" class "]" | "." | "^"
  //       | "$" | escape | literal
  bool atom(std::vector<ReInst>* out, std::string* error) {
    if (atEnd()) {
      *error = "the pattern ends where something was expected";
      return false;
    }

    if (match('(')) {
      bool capturing = true;
      if (peek() == '?') {
        if (at_ + 1 < pattern_.size() && pattern_[at_ + 1] == ':') {
          at_ += 2;
          capturing = false;
        } else {
          *error =
              "only (?: ) is supported; lookaround is not part of this engine";
          return false;
        }
      }
      int index = 0;
      if (capturing) index = ++groups_;

      std::vector<ReInst> inner;
      if (!alternate(&inner, error)) return false;
      if (!match(')')) {
        *error = "expected ')'";
        return false;
      }

      if (!capturing) {
        *out = inner;
        return true;
      }
      ReInst open;
      open.op = ReOp::Save;
      open.x = index * 2;
      out->push_back(open);
      append(out, inner);
      ReInst close;
      close.op = ReOp::Save;
      close.x = index * 2 + 1;
      out->push_back(close);
      return true;
    }

    if (match('[')) return characterClass(out, error);

    if (match('.')) {
      ReInst inst;
      inst.op = ReOp::Any;
      out->push_back(inst);
      return true;
    }
    if (match('^')) {
      ReInst inst;
      inst.op = ReOp::Bol;
      out->push_back(inst);
      return true;
    }
    if (match('$')) {
      ReInst inst;
      inst.op = ReOp::Eol;
      out->push_back(inst);
      return true;
    }
    if (peek() == '*' || peek() == '+' || peek() == '?') {
      *error = "nothing for '" + pattern_.substr(at_, 1) + "' to repeat";
      return false;
    }
    if (match(')')) {
      *error = "unmatched ')'";
      return false;
    }

    if (match('\\')) return escape(out, error);

    // An ordinary character, read as UTF-8 so that a pattern may contain
    // any text the subject can.
    uint32_t code;
    size_t width = decodeUtf8(pattern_.data(), pattern_.size(), at_, &code);
    at_ += width;
    emitChar(out, code);
    return true;
  }

  // Reads one escape outside a character class.
  bool escape(std::vector<ReInst>* out, std::string* error) {
    if (atEnd()) {
      *error = "the pattern ends after a backslash";
      return false;
    }
    char c = advance();
    switch (c) {
      case 'd':
      case 'w':
      case 's':
      case 'D':
      case 'W':
      case 'S': {
        ReInst inst;
        inst.op = ReOp::Class;
        char lower = (char)(c | 0x20);
        shorthandRanges(lower, &inst.ranges);
        inst.negated = c != lower;
        out->push_back(inst);
        return true;
      }
      case 'b': {
        ReInst inst;
        inst.op = ReOp::WordBoundary;
        out->push_back(inst);
        return true;
      }
      case 'B': {
        ReInst inst;
        inst.op = ReOp::NotWordBoundary;
        out->push_back(inst);
        return true;
      }
      default: {
        uint32_t code;
        if (!escapeValue(c, &code, error)) return false;
        emitChar(out, code);
        return true;
      }
    }
  }

  // The escapes that stand for one character, in a class or out of one.
  bool escapeValue(char c, uint32_t* out, std::string* error) {
    switch (c) {
      case 'n': *out = '\n'; return true;
      case 't': *out = '\t'; return true;
      case 'r': *out = '\r'; return true;
      case 'f': *out = '\f'; return true;
      case 'v': *out = '\v'; return true;
      case '0': *out = 0; return true;
      case 'u': {
        uint32_t code = 0;
        int digits = 0;
        bool braced = match('{');
        int wanted = braced ? 6 : 4;
        while (digits < wanted && !atEnd() && isHex(peek())) {
          code = code * 16 + (uint32_t)hexValue(advance());
          digits++;
        }
        if (digits == 0 || (!braced && digits < 4) || (braced && !match('}'))) {
          *error = "a '\\u' escape needs four hex digits, or braces around "
                   "one to six";
          return false;
        }
        if (code > kMaxCodePoint) {
          *error = "a '\\u' escape must name a code point up to 10ffff";
          return false;
        }
        *out = code;
        return true;
      }
      default:
        // Anything else stands for itself, which is how a pattern
        // escapes the characters that would otherwise be operators.
        *out = (unsigned char)c;
        return true;
    }
  }

  static bool isHex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
  }
  static int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return c - 'A' + 10;
  }

  bool characterClass(std::vector<ReInst>* out, std::string* error) {
    ReInst inst;
    inst.op = ReOp::Class;
    inst.negated = match('^');

    // A ']' straight after the opening bracket is an ordinary character,
    // which is how a class holds one at all.
    bool first = true;
    for (;;) {
      if (atEnd()) {
        *error = "expected ']' to close a character class";
        return false;
      }
      if (peek() == ']' && !first) {
        advance();
        break;
      }
      first = false;

      uint32_t low;
      bool wasShorthand = false;
      if (match('\\')) {
        if (atEnd()) {
          *error = "the pattern ends after a backslash";
          return false;
        }
        char c = advance();
        if (c == 'd' || c == 'w' || c == 's' || c == 'D' || c == 'W' ||
            c == 'S') {
          std::vector<ReRange> ranges;
          char lower = (char)(c | 0x20);
          shorthandRanges(lower, &ranges);
          if (c != lower) ranges = complement(ranges);
          for (const ReRange& range : ranges) inst.ranges.push_back(range);
          wasShorthand = true;
          low = 0;
        } else if (c == 'b') {
          low = '\b';
        } else if (!escapeValue(c, &low, error)) {
          return false;
        }
      } else {
        size_t width = decodeUtf8(pattern_.data(), pattern_.size(), at_, &low);
        at_ += width;
      }
      if (wasShorthand) continue;

      uint32_t high = low;
      // A '-' before the closing bracket is an ordinary character.
      if (peek() == '-' && at_ + 1 < pattern_.size() && pattern_[at_ + 1] != ']') {
        advance();
        if (match('\\')) {
          if (atEnd()) {
            *error = "the pattern ends after a backslash";
            return false;
          }
          if (!escapeValue(advance(), &high, error)) return false;
        } else {
          size_t width = decodeUtf8(pattern_.data(), pattern_.size(), at_, &high);
          at_ += width;
        }
        if (high < low) {
          *error = "a character range runs upwards";
          return false;
        }
      }
      inst.ranges.push_back({low, high});
      if (ignoreCase_) {
        // Folding the range rather than the subject keeps the matcher
        // free of case rules. A range wider than this is one nobody
        // wrote to mean letters, and walking it would cost more than the
        // whole match.
        constexpr uint32_t kMaxFoldSpan = 4096;
        if (high - low <= kMaxFoldSpan) {
          for (uint32_t c = low; c <= high; c++) {
            uint32_t other = swapCase(c);
            if (other != c) inst.ranges.push_back({other, other});
          }
        }
      }
    }

    if (inst.ranges.empty()) {
      *error = "an empty character class matches nothing";
      return false;
    }
    out->push_back(inst);
    return true;
  }
};

}  // namespace

bool Regex::compile(const std::string& pattern, const std::string& flags,
                    std::string* error) {
  pattern_ = pattern;
  flags_ = flags;
  ignoreCase_ = flags.find('i') != std::string::npos;
  multiLine_ = flags.find('m') != std::string::npos;
  dotAll_ = flags.find('s') != std::string::npos;

  for (char flag : flags) {
    if (flag != 'i' && flag != 'm' && flag != 's') {
      *error = std::string("unknown flag '") + flag + "', expected i, m or s";
      return false;
    }
  }

  Parser parser(pattern, ignoreCase_);
  std::vector<ReInst> body;
  if (!parser.parse(&body, error)) return false;
  groupCount_ = parser.groups();

  // The whole match is group zero, so the body is wrapped in the same
  // Save pair a group gets.
  program_.clear();
  ReInst open;
  open.op = ReOp::Save;
  open.x = 0;
  program_.push_back(open);
  append(&program_, body);
  ReInst close;
  close.op = ReOp::Save;
  close.x = 1;
  program_.push_back(close);
  ReInst done;
  done.op = ReOp::Match;
  program_.push_back(done);
  return true;
}

namespace {

struct Thread {
  int pc;
  std::vector<int> slots;
};

}  // namespace

ReMatch Regex::search(const std::string& text, size_t start) const {
  ReMatch result;
  if (program_.empty()) return result;

  const int programSize = (int)program_.size();
  std::vector<int> seen((size_t)programSize, -1);
  int generation = 0;

  std::vector<Thread> current;
  std::vector<Thread> next;
  const std::vector<int> empty((size_t)(groupCount_ + 1) * 2, -1);

  // Follows everything that does not read a character, so that the list
  // holds only threads waiting on input. Order is kept: a Split tries x
  // before y, which is what makes greedy and lazy differ.
  auto addThread = [&](std::vector<Thread>& list, int pc, size_t position,
                       const std::vector<int>& slots) {
    std::vector<std::pair<int, std::vector<int>>> pending;
    pending.push_back({pc, slots});
    while (!pending.empty()) {
      std::pair<int, std::vector<int>> item = std::move(pending.back());
      pending.pop_back();
      int at = item.first;
      if (at < 0 || at >= programSize) continue;
      if (seen[(size_t)at] == generation) continue;
      seen[(size_t)at] = generation;

      const ReInst& inst = program_[(size_t)at];
      switch (inst.op) {
        case ReOp::Jump:
          pending.push_back({inst.x, std::move(item.second)});
          break;
        case ReOp::Split:
          // Pushed in reverse so that x is taken off first.
          pending.push_back({inst.y, item.second});
          pending.push_back({inst.x, std::move(item.second)});
          break;
        case ReOp::Save: {
          std::vector<int> copy = std::move(item.second);
          if (inst.x >= 0 && (size_t)inst.x < copy.size()) {
            copy[(size_t)inst.x] = (int)position;
          }
          pending.push_back({at + 1, std::move(copy)});
          break;
        }
        case ReOp::Bol: {
          bool ok = position == 0 ||
                    (multiLine_ && text[position - 1] == '\n');
          if (ok) pending.push_back({at + 1, std::move(item.second)});
          break;
        }
        case ReOp::Eol: {
          bool ok = position == text.size() ||
                    (multiLine_ && text[position] == '\n');
          if (ok) pending.push_back({at + 1, std::move(item.second)});
          break;
        }
        case ReOp::WordBoundary:
        case ReOp::NotWordBoundary: {
          // The word characters are all ASCII, so looking at the byte on
          // each side is enough: a continuation byte is not one of them.
          bool before = position > 0 &&
                        isWordCodePoint((unsigned char)text[position - 1]);
          bool after = position < text.size() &&
                       isWordCodePoint((unsigned char)text[position]);
          bool boundary = before != after;
          if (boundary == (inst.op == ReOp::WordBoundary)) {
            pending.push_back({at + 1, std::move(item.second)});
          }
          break;
        }
        default:
          list.push_back({at, std::move(item.second)});
          break;
      }
    }
  };

  auto classMatches = [&](const ReInst& inst, uint32_t code) {
    bool inside = false;
    for (const ReRange& range : inst.ranges) {
      if (code >= range.lo && code <= range.hi) {
        inside = true;
        break;
      }
    }
    return inside != inst.negated;
  };

  size_t position = start > text.size() ? text.size() : start;
  generation++;
  addThread(current, 0, position, empty);

  for (;;) {
    bool atEnd = position >= text.size();
    uint32_t code = 0;
    size_t width = 1;
    if (!atEnd) {
      width = decodeUtf8(text.data(), text.size(), position, &code);
    }

    generation++;
    next.clear();

    for (size_t i = 0; i < current.size(); i++) {
      const ReInst& inst = program_[(size_t)current[i].pc];
      if (inst.op == ReOp::Match) {
        result.matched = true;
        result.slots = current[i].slots;
        // Every thread after this one is a lower preference, so they are
        // dropped rather than run.
        break;
      }
      if (atEnd) continue;

      bool takes = false;
      switch (inst.op) {
        case ReOp::Char:
          takes = inst.ch == code ||
                  (ignoreCase_ && swapCase(inst.ch) == swapCase(code) &&
                   swapCase(code) != code);
          break;
        case ReOp::Any:
          takes = dotAll_ || code != '\n';
          break;
        case ReOp::Class:
          takes = classMatches(inst, code);
          break;
        default:
          break;
      }
      if (takes) {
        addThread(next, current[i].pc + 1, position + width, current[i].slots);
      }
    }

    // A fresh attempt starting here, behind everything already running,
    // so that an earlier start always wins. Once something has matched
    // there is no point starting anything later.
    if (!result.matched && !atEnd) {
      addThread(next, 0, position + width, empty);
    }

    if (atEnd) break;
    position += width;
    current.swap(next);
    // An empty list only ends the search once something has matched.
    // Before that a fresh attempt is started at every position, and the
    // one at this position may have died on an anchor rather than on the
    // input, which says nothing about the next.
    if (current.empty() && result.matched) break;
  }

  return result;
}

}  // namespace red
