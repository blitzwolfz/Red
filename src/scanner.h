// Hand written lexer.
//
// String interpolation is handled here rather than in the compiler. When
// the scanner meets `${` inside a string it emits the literal text so far
// as a StringInterp token and then returns to normal scanning. The closing
// `}` puts it back into string mode. A brace depth counter per open
// interpolation lets expressions contain map literals and blocks.
#pragma once

#include <string>
#include <vector>

#include "common.h"

namespace red {

enum class TokenType {
  LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
  Comma, Dot, Minus, Plus, Semicolon, Slash, Star, Percent, Colon, Arrow,

  Bang, BangEqual, Equal, EqualEqual,
  Greater, GreaterEqual, Less, LessEqual,

  Identifier, String, StringInterp, Number,

  And, Break, Catch, Class, Const, Continue, Else, False, For, Fun, If,
  Import, Let, Nil, Or, Return, Spawn, Super, This, Throw, True, Try, While,
  As,

  // Added after the first release. New members go here, immediately
  // before Error, so that the parse rule table in compiler.cpp keeps its
  // existing indexes.
  Ampersand, Pipe, Caret, Tilde, LessLess, GreaterGreater,
  PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
  In, Switch, Case, Default, Ellipsis,

  Error, Eof,
};

struct Token {
  TokenType type = TokenType::Eof;
  std::string lexeme;
  // Decoded contents for String and StringInterp tokens. Escape sequences
  // are already resolved.
  std::string text;
  double number = 0;
  int line = 1;
};

class Scanner {
 public:
  explicit Scanner(const std::string& source);
  Token scan();

 private:
  const std::string& source_;
  size_t start_ = 0;
  size_t current_ = 0;
  int line_ = 1;
  // One entry per interpolation currently open. The value is the number of
  // unmatched `{` seen inside that interpolation.
  std::vector<int> interpolation_;

  bool atEnd() const { return current_ >= source_.size(); }
  char advance() { return source_[current_++]; }
  char peek() const { return atEnd() ? '\0' : source_[current_]; }
  char peekNext() const {
    return current_ + 1 >= source_.size() ? '\0' : source_[current_ + 1];
  }
  bool match(char expected);
  void skipWhitespace();

  Token make(TokenType type) const;
  Token errorToken(const std::string& message) const;
  Token string();
  // Continues a string after an interpolation closed.
  Token resumeString();
  Token number();
  Token identifier();
  TokenType identifierType() const;
};

}  // namespace red
