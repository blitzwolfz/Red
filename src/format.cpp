#include "format.h"

#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "util.h"

namespace red {

namespace {

enum class Piece {
  Comment,
  String,
  Number,
  Word,     // an identifier or a keyword
  Punct,
  End,
};

struct Token {
  Piece kind = Piece::End;
  std::string text;
  // Line breaks in the source before this token. Zero means it follows
  // the one before it on the same line.
  int breaksBefore = 0;
};

bool isWordStart(char c) {
  return std::isalpha((unsigned char)c) != 0 || c == '_';
}
bool isWordPart(char c) {
  return isWordStart(c) || std::isdigit((unsigned char)c) != 0;
}

bool isKeyword(const std::string& word) {
  static const char* kWords[] = {
      "and",   "as",    "break", "case",    "catch", "class",  "const",
      "continue", "default", "else", "enum", "false", "finally", "for",
      "fun",   "if",    "import", "in",    "let",   "nil",    "or",
      "return", "spawn", "super", "switch", "this",  "throw",  "true",
      "try",   "while"};
  for (const char* candidate : kWords) {
    if (word == candidate) return true;
  }
  return false;
}

// A keyword that takes a parenthesised clause after it, and so wants a
// space before the bracket where a function name would not.
bool takesClause(const std::string& word) {
  return word == "if" || word == "while" || word == "for" ||
         word == "switch" || word == "catch" || word == "return" ||
         word == "throw" || word == "spawn" || word == "in" ||
         word == "and" || word == "or" || word == "else" || word == "case";
}

class Lexer {
 public:
  Lexer(const std::string& source, std::string* error)
      : source_(source), error_(error) {}

  bool run(std::vector<Token>* out) {
    for (;;) {
      int breaks = skipBlanks();
      if (at_ >= source_.size()) return true;

      Token token;
      token.breaksBefore = breaks;
      if (!next(&token)) return false;
      out->push_back(token);
    }
  }

 private:
  const std::string& source_;
  std::string* error_;
  size_t at_ = 0;

  // Skips spaces, counting the line breaks so that the writer can keep
  // the author's own line structure.
  int skipBlanks() {
    int breaks = 0;
    while (at_ < source_.size()) {
      char c = source_[at_];
      if (c == '\n') {
        breaks++;
        at_++;
      } else if (c == ' ' || c == '\t' || c == '\r') {
        at_++;
      } else {
        break;
      }
    }
    return breaks;
  }

  bool next(Token* token) {
    char c = source_[at_];

    if (c == '/' && at_ + 1 < source_.size() && source_[at_ + 1] == '/') {
      size_t start = at_;
      while (at_ < source_.size() && source_[at_] != '\n') at_++;
      token->kind = Piece::Comment;
      token->text = source_.substr(start, at_ - start);
      // A trailing space before the newline is not part of the comment.
      while (!token->text.empty() && token->text.back() == ' ') {
        token->text.pop_back();
      }
      return true;
    }

    if (c == '/' && at_ + 1 < source_.size() && source_[at_ + 1] == '*') {
      size_t start = at_;
      at_ += 2;
      int depth = 1;
      while (at_ < source_.size() && depth > 0) {
        if (source_.compare(at_, 2, "/*") == 0) {
          depth++;
          at_ += 2;
        } else if (source_.compare(at_, 2, "*/") == 0) {
          depth--;
          at_ += 2;
        } else {
          at_++;
        }
      }
      if (depth > 0) {
        *error_ = "unterminated block comment";
        return false;
      }
      token->kind = Piece::Comment;
      token->text = source_.substr(start, at_ - start);
      return true;
    }

    if (c == '"') return string(token);

    if (std::isdigit((unsigned char)c) != 0) {
      size_t start = at_;
      if (source_.compare(at_, 2, "0x") == 0 ||
          source_.compare(at_, 2, "0X") == 0) {
        at_ += 2;
        while (at_ < source_.size() &&
               std::isxdigit((unsigned char)source_[at_]) != 0) {
          at_++;
        }
      } else {
        while (at_ < source_.size() &&
               std::isdigit((unsigned char)source_[at_]) != 0) {
          at_++;
        }
        if (at_ < source_.size() && source_[at_] == '.' &&
            at_ + 1 < source_.size() &&
            std::isdigit((unsigned char)source_[at_ + 1]) != 0) {
          at_++;
          while (at_ < source_.size() &&
                 std::isdigit((unsigned char)source_[at_]) != 0) {
            at_++;
          }
        }
        if (at_ < source_.size() &&
            (source_[at_] == 'e' || source_[at_] == 'E')) {
          size_t save = at_;
          at_++;
          if (at_ < source_.size() &&
              (source_[at_] == '+' || source_[at_] == '-')) {
            at_++;
          }
          if (at_ < source_.size() &&
              std::isdigit((unsigned char)source_[at_]) != 0) {
            while (at_ < source_.size() &&
                   std::isdigit((unsigned char)source_[at_]) != 0) {
              at_++;
            }
          } else {
            at_ = save;
          }
        }
      }
      token->kind = Piece::Number;
      token->text = source_.substr(start, at_ - start);
      return true;
    }

    if (isWordStart(c)) {
      size_t start = at_;
      while (at_ < source_.size() && isWordPart(source_[at_])) at_++;
      token->kind = Piece::Word;
      token->text = source_.substr(start, at_ - start);
      return true;
    }

    // The two and three character operators, longest first.
    static const char* kOperators[] = {"...", "<<", ">>", "==", "!=", "<=",
                                       ">=", "+=", "-=", "*=", "/=", "%=",
                                       "->"};
    for (const char* op : kOperators) {
      size_t length = std::strlen(op);
      if (source_.compare(at_, length, op) == 0) {
        token->kind = Piece::Punct;
        token->text = op;
        at_ += length;
        return true;
      }
    }

    token->kind = Piece::Punct;
    token->text = std::string(1, c);
    at_++;
    return true;
  }

  // A string literal, taken whole, interpolations and all. What is
  // inside `${ }` is code, but reformatting it would mean deciding where
  // the string ends and the code begins on every line, and the payoff is
  // not worth the risk of moving a character that was meant to be there.
  bool string(Token* token) {
    size_t start = at_;
    at_++;  // the opening quote
    std::vector<int> interpolation;

    while (at_ < source_.size()) {
      char c = source_[at_];
      if (c == '\\' && at_ + 1 < source_.size()) {
        at_ += 2;
        continue;
      }
      if (interpolation.empty()) {
        if (c == '"') {
          at_++;
          token->kind = Piece::String;
          token->text = source_.substr(start, at_ - start);
          return true;
        }
        if (c == '$' && at_ + 1 < source_.size() && source_[at_ + 1] == '{') {
          interpolation.push_back(0);
          at_ += 2;
          continue;
        }
        at_++;
        continue;
      }
      // Inside an interpolation: count braces so that a map literal or a
      // block does not end it early.
      if (c == '{') {
        interpolation.back()++;
      } else if (c == '}') {
        if (interpolation.back() == 0) {
          interpolation.pop_back();
        } else {
          interpolation.back()--;
        }
      } else if (c == '"') {
        // A string inside the interpolation. Skip it whole.
        at_++;
        while (at_ < source_.size() && source_[at_] != '"') {
          if (source_[at_] == '\\') at_++;
          at_++;
        }
      }
      at_++;
    }
    *error_ = "unterminated string";
    return false;
  }
};

// Is this `{` opening a map literal rather than a block? A map literal
// appears where a value is expected, and a block where a statement is.
// The token in front says which.
bool opensMapLiteral(const Token* previous) {
  if (previous == nullptr) return false;
  if (previous->kind == Piece::Word) {
    // `return {}` is a value; `else {` and `try {` are blocks. Anything
    // that is not a keyword is a name, and a name is never followed by a
    // block brace.
    const std::string& p = previous->text;
    // `let {a}` and `const {a}` are destructuring patterns, which are
    // written tight like the map they take apart.
    return p == "return" || p == "and" || p == "or" || p == "in" ||
           p == "throw" || p == "case" || p == "let" || p == "const";
  }
  const std::string& p = previous->text;
  return p == "=" || p == "(" || p == "," || p == "[" || p == ":" ||
         p == "+" || p == "-" || p == "*" || p == "/" || p == "%" ||
         p == "==" || p == "!=" || p == "<" || p == ">" || p == "<=" ||
         p == ">=" || p == "+=" || p == "-=" || p == "*=" || p == "/=" ||
         p == "%=";
}

// Should there be a space between these two tokens on one line?
bool wantsSpace(const Token& left, const Token& right, bool rightIsUnary,
                bool insideMap) {
  const std::string& a = left.text;
  const std::string& b = right.text;

  // Never before these.
  if (b == "," || b == ";" || b == ")" || b == "]" || b == ".") return false;
  // Never after these.
  if (a == "(" || a == "[" || a == ".") return false;
  // A map literal is written tight: {"a": 1}. A block is not: { a; }.
  if (insideMap && (a == "{" || b == "}")) return false;
  if (a == "!" || a == "~") return false;
  // A unary minus binds to what follows it.
  if (rightIsUnary) return false;
  if (left.kind == Piece::Punct && (a == "-" || a == "+") &&
      left.text.size() == 1 && false) {
    return true;
  }

  // `...rest` and `-x` after an operator.
  if (a == "...") return false;

  // A call binds to the name in front of it; a keyword that takes a
  // clause does not, and `fun (` is the anonymous form.
  if (b == "(") {
    if (left.kind == Piece::Word) return takesClause(a) || a == "fun";
    if (a == ")" || a == "]") return false;
    // An operator, a comma or an `=`: `x = (a | b)`.
    return true;
  }
  // An index binds the same way. `let [a, b]` is a pattern, not an
  // index, and a keyword in front is what tells them apart.
  if (b == "[") {
    if (left.kind == Piece::Word) return isKeyword(a);
    if (left.kind == Piece::String || left.kind == Piece::Number) return false;
    if (a == ")" || a == "]") return false;
    return true;
  }

  // A colon: none before, one after. That covers a map key, a type
  // annotation, a case label and a catch filter alike.
  if (b == ":") return false;
  if (a == ":") return true;
  if (a == "," || a == ";") return true;

  return true;
}

// Is this `-` or `+` a sign rather than an operator?
bool isUnaryHere(const Token* previous, const Token& token) {
  if (token.text != "-" && token.text != "+") return false;
  if (previous == nullptr) return true;
  if (previous->kind == Piece::Number || previous->kind == Piece::String) {
    return false;
  }
  if (previous->kind == Piece::Word) {
    // `return -1` is a sign; `x - 1` is an operator.
    return isKeyword(previous->text);
  }
  const std::string& p = previous->text;
  return p != ")" && p != "]" && p != "}";
}

// How wide a piece of text looks, in characters rather than bytes, so
// that a comment after a line holding text is lined up where the eye
// expects rather than where the bytes fall.
size_t displayWidth(const std::string& text) {
  size_t width = 0;
  size_t i = 0;
  while (i < text.size()) {
    uint32_t code;
    i += decodeUtf8(text.data(), text.size(), i, &code);
    width++;
  }
  return width;
}

// Lines a comment on the end of a line up with the ones above and below
// it, so that a column of them stays a column. A run ends at the first
// line that has no trailing comment, which is what keeps one stray
// comment from pulling a whole file out of shape.
void alignTrailingComments(std::vector<std::string>* lines,
                           const std::vector<int>* commentAt) {
  size_t i = 0;
  while (i < lines->size()) {
    if ((*commentAt)[i] < 0) {
      i++;
      continue;
    }
    size_t end = i;
    size_t widest = 0;
    while (end < lines->size() && (*commentAt)[end] >= 0) {
      std::string code = (*lines)[end].substr(0, (size_t)(*commentAt)[end]);
      while (!code.empty() && code.back() == ' ') code.pop_back();
      size_t width = displayWidth(code);
      if (width > widest) widest = width;
      end++;
    }
    for (size_t k = i; k < end; k++) {
      std::string code = (*lines)[k].substr(0, (size_t)(*commentAt)[k]);
      std::string comment = (*lines)[k].substr((size_t)(*commentAt)[k]);
      while (!code.empty() && code.back() == ' ') code.pop_back();
      size_t pad = widest - displayWidth(code) + 1;
      (*lines)[k] = code + std::string(pad, ' ') + comment;
    }
    i = end;
  }
}

std::string render(const std::vector<Token>& tokens) {
  std::string out;
  int depth = 0;
  bool lineStarted = false;
  const Token* previous = nullptr;
  bool previousWasUnary = false;
  // One entry per open brace. A switch body indents its statements one
  // further than its case labels, which no brace of its own marks out.
  enum class Brace { Block, Map, Switch };
  std::vector<Brace> braces;
  bool pendingSwitch = false;
  // The finished lines, and where a comment begins on each one when
  // there is code in front of it.
  std::vector<std::string> lines;
  std::vector<int> commentAt;
  std::string line;
  int lineComment = -1;
  bool lineHasCode = false;

  auto endLine = [&]() {
    lines.push_back(line);
    commentAt.push_back(lineComment);
    line.clear();
    lineComment = -1;
    lineHasCode = false;
  };

  for (size_t i = 0; i < tokens.size(); i++) {
    const Token& token = tokens[i];

    if (i > 0 && token.breaksBefore > 0) {
      endLine();
      // Several blank lines in a row become one. None are added.
      if (token.breaksBefore > 1) endLine();
      lineStarted = false;
    }

    if (!lineStarted) {
      // A line that begins by closing something is indented with what it
      // closes, not with what is inside it.
      int indent = depth;
      if (token.text == "}" || token.text == ")" || token.text == "]") {
        indent--;
      } else if (!braces.empty() && braces.back() == Brace::Switch &&
                 !(token.kind == Piece::Word &&
                   (token.text == "case" || token.text == "default"))) {
        // A statement belonging to a case, rather than the label itself.
        indent++;
      }
      if (indent < 0) indent = 0;
      line.append((size_t)indent * 2, ' ');
      lineStarted = true;
    } else if (previous != nullptr) {
      // The brace that is about to close, or the one just opened, is the
      // one whose kind decides the spacing.
      bool insideMap = false;
      if (token.text == "}" || previous->text == "{") {
        insideMap = !braces.empty() && braces.back() == Brace::Map;
      }
      if (wantsSpace(*previous, token, previousWasUnary, insideMap)) {
        line += ' ';
      }
    }

    if (token.kind == Piece::Comment && lineHasCode && lineComment < 0) {
      lineComment = (int)line.size();
    } else if (token.kind != Piece::Comment) {
      lineHasCode = true;
    }
    line += token.text;
    if (token.kind == Piece::Word && token.text == "switch") {
      pendingSwitch = true;
    }
    if (token.text == "{") {
      if (pendingSwitch) {
        braces.push_back(Brace::Switch);
        pendingSwitch = false;
      } else if (opensMapLiteral(previous)) {
        braces.push_back(Brace::Map);
      } else {
        braces.push_back(Brace::Block);
      }
      depth++;
    } else if (token.text == "(" || token.text == "[") {
      depth++;
    } else if (token.text == "}") {
      if (!braces.empty()) braces.pop_back();
      depth--;
    } else if (token.text == ")" || token.text == "]") {
      depth--;
    }
    if (depth < 0) depth = 0;

    previousWasUnary = isUnaryHere(previous, token);
    previous = &token;
  }
  endLine();

  alignTrailingComments(&lines, &commentAt);
  for (const std::string& finished : lines) {
    out += finished;
    out += '\n';
  }
  return out;
}

bool sameTokens(const std::vector<Token>& a, const std::vector<Token>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) {
    if (a[i].kind != b[i].kind || a[i].text != b[i].text) return false;
  }
  return true;
}

}  // namespace

bool formatSource(const std::string& source, std::string* out,
                  std::string* error) {
  std::vector<Token> tokens;
  Lexer lexer(source, error);
  if (!lexer.run(&tokens)) return false;

  if (tokens.empty()) {
    out->clear();
    return true;
  }

  *out = render(tokens);

  // The formatter is only allowed to move whitespace. Lexing the result
  // and comparing catches any rule above that dropped, joined or split
  // something, and turns it into a refusal instead of a mangled file.
  std::vector<Token> again;
  std::string ignored;
  Lexer check(*out, &ignored);
  if (!check.run(&again) || !sameTokens(tokens, again)) {
    *error =
        "the formatted text does not read back the same. This is a bug in "
        "the formatter; the file has been left alone";
    return false;
  }
  return true;
}

}  // namespace red
