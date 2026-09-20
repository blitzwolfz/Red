#include "scanner.h"

#include <cstdlib>
#include <unordered_map>

namespace red {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isAlpha(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

const std::unordered_map<std::string, TokenType>& keywords() {
  static const std::unordered_map<std::string, TokenType> table = {
      {"and", TokenType::And},       {"as", TokenType::As},
      {"case", TokenType::Case},     {"default", TokenType::Default},
      {"in", TokenType::In},         {"switch", TokenType::Switch},
      {"break", TokenType::Break},   {"catch", TokenType::Catch},
      {"class", TokenType::Class},   {"const", TokenType::Const},
      {"continue", TokenType::Continue}, {"else", TokenType::Else},
      {"false", TokenType::False},   {"for", TokenType::For},
      {"fun", TokenType::Fun},       {"if", TokenType::If},
      {"import", TokenType::Import}, {"let", TokenType::Let},
      {"nil", TokenType::Nil},       {"or", TokenType::Or},
      {"return", TokenType::Return}, {"spawn", TokenType::Spawn},
      {"super", TokenType::Super},   {"this", TokenType::This},
      {"throw", TokenType::Throw},   {"true", TokenType::True},
      {"try", TokenType::Try},       {"while", TokenType::While},
  };
  return table;
}

}  // namespace

Scanner::Scanner(const std::string& source) : source_(source) {}

bool Scanner::match(char expected) {
  if (atEnd() || source_[current_] != expected) return false;
  current_++;
  return true;
}

void Scanner::skipWhitespace() {
  for (;;) {
    char c = peek();
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
        advance();
        break;
      case '\n':
        line_++;
        advance();
        break;
      case '/':
        if (peekNext() == '/') {
          while (peek() != '\n' && !atEnd()) advance();
        } else if (peekNext() == '*') {
          advance();
          advance();
          // Block comments nest, which makes commenting out a region that
          // already contains a comment work.
          int depth = 1;
          while (depth > 0 && !atEnd()) {
            if (peek() == '\n') line_++;
            if (peek() == '/' && peekNext() == '*') {
              depth++;
              advance();
              advance();
            } else if (peek() == '*' && peekNext() == '/') {
              depth--;
              advance();
              advance();
            } else {
              advance();
            }
          }
        } else {
          return;
        }
        break;
      default:
        return;
    }
  }
}

Token Scanner::make(TokenType type) const {
  Token token;
  token.type = type;
  token.lexeme = source_.substr(start_, current_ - start_);
  token.line = line_;
  return token;
}

Token Scanner::errorToken(const std::string& message) const {
  Token token;
  token.type = TokenType::Error;
  token.lexeme = message;
  token.line = line_;
  return token;
}

Token Scanner::number() {
  while (isDigit(peek())) advance();
  if (peek() == '.' && isDigit(peekNext())) {
    advance();
    while (isDigit(peek())) advance();
  }
  // Exponent form, so 1e9 does not have to be written out.
  if (peek() == 'e' || peek() == 'E') {
    size_t save = current_;
    advance();
    if (peek() == '+' || peek() == '-') advance();
    if (isDigit(peek())) {
      while (isDigit(peek())) advance();
    } else {
      current_ = save;
    }
  }
  Token token = make(TokenType::Number);
  token.number = std::strtod(token.lexeme.c_str(), nullptr);
  return token;
}

Token Scanner::identifier() {
  while (isAlpha(peek()) || isDigit(peek())) advance();
  return make(identifierType());
}

TokenType Scanner::identifierType() const {
  std::string text = source_.substr(start_, current_ - start_);
  auto it = keywords().find(text);
  return it == keywords().end() ? TokenType::Identifier : it->second;
}

// Shared body for a string literal and for the part after an
// interpolation closes. Stops at the closing quote or at `${`.
Token Scanner::resumeString() {
  std::string value;
  for (;;) {
    if (atEnd()) return errorToken("Unterminated string.");
    char c = advance();
    if (c == '"') {
      Token token = make(TokenType::String);
      token.text = value;
      return token;
    }
    if (c == '\n') {
      line_++;
      value += c;
      continue;
    }
    if (c == '\\') {
      if (atEnd()) return errorToken("Unterminated escape sequence.");
      char escape = advance();
      switch (escape) {
        case 'n': value += '\n'; break;
        case 't': value += '\t'; break;
        case 'r': value += '\r'; break;
        case '0': value += '\0'; break;
        case '\\': value += '\\'; break;
        case '"': value += '"'; break;
        case '$': value += '$'; break;
        default:
          return errorToken(std::string("Unknown escape sequence '\\") +
                            escape + "'.");
      }
      continue;
    }
    if (c == '$' && peek() == '{') {
      advance();
      // Hand the literal part back and switch to expression scanning. The
      // matching '}' is found by the brace counter below.
      interpolation_.push_back(0);
      Token token = make(TokenType::StringInterp);
      token.text = value;
      return token;
    }
    value += c;
  }
}

Token Scanner::string() { return resumeString(); }

Token Scanner::scan() {
  skipWhitespace();
  start_ = current_;
  if (atEnd()) return make(TokenType::Eof);

  char c = advance();
  if (isAlpha(c)) return identifier();
  if (isDigit(c)) return number();

  switch (c) {
    case '(': return make(TokenType::LeftParen);
    case ')': return make(TokenType::RightParen);
    case '[': return make(TokenType::LeftBracket);
    case ']': return make(TokenType::RightBracket);
    case '{':
      if (!interpolation_.empty()) interpolation_.back()++;
      return make(TokenType::LeftBrace);
    case '}':
      if (!interpolation_.empty()) {
        if (interpolation_.back() == 0) {
          // This brace closes an interpolation, not a block. Go back to
          // reading string characters.
          interpolation_.pop_back();
          return resumeString();
        }
        interpolation_.back()--;
      }
      return make(TokenType::RightBrace);
    case ';': return make(TokenType::Semicolon);
    case ',': return make(TokenType::Comma);
    case '.':
      // "..." marks a rest parameter. A single dot is property access.
      if (peek() == '.' && peekNext() == '.') {
        advance();
        advance();
        return make(TokenType::Ellipsis);
      }
      return make(TokenType::Dot);
    case ':': return make(TokenType::Colon);
    case '%':
      return make(match('=') ? TokenType::PercentEqual : TokenType::Percent);
    case '-':
      if (match('>')) return make(TokenType::Arrow);
      return make(match('=') ? TokenType::MinusEqual : TokenType::Minus);
    case '+': return make(match('=') ? TokenType::PlusEqual : TokenType::Plus);
    case '/': return make(match('=') ? TokenType::SlashEqual : TokenType::Slash);
    case '*': return make(match('=') ? TokenType::StarEqual : TokenType::Star);
    case '&': return make(TokenType::Ampersand);
    case '|': return make(TokenType::Pipe);
    case '^': return make(TokenType::Caret);
    case '~': return make(TokenType::Tilde);
    case '!': return make(match('=') ? TokenType::BangEqual : TokenType::Bang);
    case '=': return make(match('=') ? TokenType::EqualEqual : TokenType::Equal);
    case '<':
      if (match('<')) return make(TokenType::LessLess);
      return make(match('=') ? TokenType::LessEqual : TokenType::Less);
    case '>':
      if (match('>')) return make(TokenType::GreaterGreater);
      return make(match('=') ? TokenType::GreaterEqual : TokenType::Greater);
    case '"': return string();
    default: break;
  }
  return errorToken(std::string("Unexpected character '") + c + "'.");
}

}  // namespace red
