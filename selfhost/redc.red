// The Red compiler, written in Red.
//
//   red selfhost/redc.red compile program.red -o program.redc
//   red selfhost/redc.red disasm program.red
//
// This is stage 3 of docs/bootstrapping.md: a port of src/scanner.cpp,
// src/compiler.cpp and src/serialize.cpp into the language they compile.
// It is a port and not a redesign. Where the C++ does something in an odd
// order, this does it in the same odd order, because the two are checked
// against each other byte for byte.
//
// It is one file on purpose. `import` is resolved when a program runs,
// not when it is compiled, so a compiler split across modules would still
// pull its own parts through the C++ compiler at start-up and the
// bootstrap would prove nothing. One file compiles to one chunk that
// depends on nothing.
//
// See selfhost/README.md for how the three stage bootstrap is run.

// ---------------------------------------------------------------------
// Bytecode

// Version of the compiled file format. Must match kBytecodeVersion in
// src/common.h.
const BYTECODE_VERSION = 4;

// Instructions, in the order src/chunk.h declares them. The numbering is
// the format, so members are never reordered, only appended.
enum Op {
  Constant, Nil, True, False, Pop,
  GetLocal, SetLocal, GetGlobal, SetGlobal, DefineGlobal,
  GetUpvalue, SetUpvalue, GetProperty, SetProperty, GetSuper,
  Equal, NotEqual, Greater, GreaterEqual, Less, LessEqual,
  Add, Subtract, Multiply, Divide, Modulo, Negate, Not,
  Jump, JumpIfFalse, JumpIfTrue, Loop,
  Call, Invoke, SuperInvoke, Closure, CloseUpvalue, Return,
  Class, Inherit, Method,
  Array, Map, GetIndex, SetIndex,
  ToString,
  TryBegin, TryEnd, Throw,
  Spawn, Import,
  Dup, Dup2,
  BitAnd, BitOr, BitXor, BitNot, ShiftLeft, ShiftRight,
  IterPrep, IterNext, JumpIfArg, CatchMatches,
  DestructureIndex, DestructureRest, DestructureField,
  CheckType, CheckLocal, Is
}

// ---------------------------------------------------------------------
// Tokens

// Token kinds, in the order src/scanner.h declares them. The parse rule
// table below is indexed by a member's value, so the order is load
// bearing and new members are appended, never inserted.
enum Tok {
  LeftParen, RightParen, LeftBrace, RightBrace, LeftBracket, RightBracket,
  Comma, Dot, Minus, Plus, Semicolon, Slash, Star, Percent, Colon, Arrow,

  Bang, BangEqual, Equal, EqualEqual,
  Greater, GreaterEqual, Less, LessEqual,

  Identifier, String, StringInterp, Number,

  And, Break, Catch, Class, Const, Continue, Else, False, For, Fun, If,
  Import, Let, Nil, Or, Return, Spawn, Super, This, Throw, True, Try, While,
  As,

  Ampersand, Pipe, Caret, Tilde, LessLess, GreaterGreater,
  PlusEqual, MinusEqual, StarEqual, SlashEqual, PercentEqual,
  In, Switch, Case, Default, Ellipsis, Enum, Finally,
  Question, Is,

  Error, Eof
}

const TOKEN_KINDS = 74;

// Binding power, weakest first. These are plain numbers rather than enum
// members because binary() needs `precedence + 1`.
const P_NONE = 0;
const P_ASSIGNMENT = 1; // =
const P_OR = 2;         // or
const P_AND = 3;        // and
const P_EQUALITY = 4;   // == !=
const P_COMPARISON = 5; // < > <= >=
const P_BIT_OR = 6;     // |
const P_BIT_XOR = 7;    // ^
const P_BIT_AND = 8;    // &
const P_SHIFT = 9;      // << >>
const P_TERM = 10;      // + -
const P_FACTOR = 11;    // * / %
const P_UNARY = 12;     // ! - ~
const P_CALL = 13;      // . () []
const P_PRIMARY = 14;

// What kind of body is being compiled.
const K_SCRIPT = 0;
const K_FUNCTION = 1;
const K_METHOD = 2;
const K_INITIALIZER = 3;

// What the code after a finally block has to do next. These numbers are
// written into the bytecode as constants, so they match FinallyAction in
// src/compiler.cpp.
const F_FALL_THROUGH = 0;
const F_RETHROW = 1;
const F_RETURN = 2;
const F_BREAK = 3;
const F_CONTINUE = 4;

// Where one binding of a destructuring pattern reads from.
const PS_INDEX = 0;
const PS_FIELD = 1;
const PS_REST = 2;

const MAX_LOCALS = 256;
const MAX_UPVALUES = 256;

class Token {
  init(type, lexeme, text, number, line) {
    this.type = type;
    this.lexeme = lexeme;
    // Decoded contents of a String or StringInterp token, escapes already
    // resolved.
    this.text = text;
    this.number = number;
    this.line = line;
  }
}

fun keywordTable() {
  return {
    "and": Tok.And, "as": Tok.As, "break": Tok.Break, "case": Tok.Case,
    "catch": Tok.Catch, "class": Tok.Class, "const": Tok.Const,
    "continue": Tok.Continue, "default": Tok.Default, "else": Tok.Else,
    "enum": Tok.Enum, "false": Tok.False, "finally": Tok.Finally,
    "for": Tok.For, "fun": Tok.Fun, "if": Tok.If, "import": Tok.Import,
    "in": Tok.In, "is": Tok.Is, "let": Tok.Let, "nil": Tok.Nil,
    "or": Tok.Or,
    "return": Tok.Return, "spawn": Tok.Spawn, "super": Tok.Super,
    "switch": Tok.Switch, "this": Tok.This, "throw": Tok.Throw,
    "true": Tok.True, "try": Tok.Try, "while": Tok.While,
  };
}

// ---------------------------------------------------------------------
// Scanner
//
// A port of src/scanner.cpp. String interpolation is handled here rather
// than in the compiler: at `${` the literal text so far comes back as a
// StringInterp token and scanning returns to ordinary mode, and the
// matching `}` puts it back into string mode. One brace counter per open
// interpolation lets an interpolated expression contain blocks and maps.

class Scanner {
  init(source) {
    this.source = source;
    this.length = source.len();
    this.start = 0;
    this.current = 0;
    this.line = 1;
    // One entry per interpolation currently open, holding the number of
    // unmatched '{' seen inside it.
    this.interpolation = [];
    this.keywords = keywordTable();
  }

  atEnd() { return this.current >= this.length; }

  advance() {
    const c = this.source[this.current];
    this.current += 1;
    return c;
  }

  peek() {
    if (this.current >= this.length) { return ""; }
    return this.source[this.current];
  }

  peekNext() {
    if (this.current + 1 >= this.length) { return ""; }
    return this.source[this.current + 1];
  }

  match(expected) {
    if (this.current >= this.length) { return false; }
    if (this.source[this.current] != expected) { return false; }
    this.current += 1;
    return true;
  }

  skipWhitespace() {
    for (;;) {
      const c = this.peek();
      if (c == " " or c == "\r" or c == "\t") {
        this.current += 1;
      } else if (c == "\n") {
        this.line += 1;
        this.current += 1;
      } else if (c == "/") {
        const next = this.peekNext();
        if (next == "/") {
          while (this.peek() != "\n" and !this.atEnd()) { this.current += 1; }
        } else if (next == "*") {
          this.current += 2;
          // Block comments nest, so commenting out a region that already
          // holds a comment works.
          let depth = 1;
          while (depth > 0 and !this.atEnd()) {
            const here = this.peek();
            if (here == "\n") { this.line += 1; }
            if (here == "/" and this.peekNext() == "*") {
              depth += 1;
              this.current += 2;
            } else if (here == "*" and this.peekNext() == "/") {
              depth -= 1;
              this.current += 2;
            } else {
              this.current += 1;
            }
          }
        } else {
          return;
        }
      } else {
        return;
      }
    }
  }

  make(type) {
    return Token(type, this.source.sub(this.start, this.current), "", 0,
      this.line);
  }

  errorToken(message) {
    return Token(Tok.Error, message, "", 0, this.line);
  }

  isDigit(c) { return c >= "0" and c <= "9"; }

  isAlpha(c) {
    return (c >= "a" and c <= "z") or (c >= "A" and c <= "Z") or c == "_";
  }

  isHexDigit(c) {
    return (c >= "0" and c <= "9") or (c >= "a" and c <= "f") or
    (c >= "A" and c <= "F");
  }

  hexValue(c) {
    const code = c.code_at(0);
    if (code >= 48 and code <= 57) { return code - 48; }
    if (code >= 97 and code <= 102) { return code - 87; }
    return code - 55;
  }

  number() {
    // 0x1f. Written out as a double the same way every other literal is,
    // so a hex literal past 2^53 loses its low bits like any other
    // number that large rather than meaning something different.
    const next = this.peek();
    if (this.source[this.start] == "0" and (next == "x" or next == "X") and
      this.isHexDigit(this.peekNext())) {
      this.current += 1;
      let value = 0;
      while (this.isHexDigit(this.peek())) {
        value = value * 16 + this.hexValue(this.advance());
      }
      const hex = this.make(Tok.Number);
      hex.number = value;
      return hex;
    }

    while (this.isDigit(this.peek())) { this.current += 1; }
    if (this.peek() == "." and this.isDigit(this.peekNext())) {
      this.current += 1;
      while (this.isDigit(this.peek())) { this.current += 1; }
    }
    // Exponent form, so 1e9 does not have to be written out.
    const c = this.peek();
    if (c == "e" or c == "E") {
      const save = this.current;
      this.current += 1;
      const sign = this.peek();
      if (sign == "+" or sign == "-") { this.current += 1; }
      if (this.isDigit(this.peek())) {
        while (this.isDigit(this.peek())) { this.current += 1; }
      } else {
        this.current = save;
      }
    }
    const token = this.make(Tok.Number);
    // num() is strtod, the same conversion the C++ scanner uses, so the
    // two agree on every literal down to the last bit.
    token.number = num(token.lexeme);
    return token;
  }

  identifier() {
    while (this.isAlpha(this.peek()) or this.isDigit(this.peek())) {
      this.current += 1;
    }
    const text = this.source.sub(this.start, this.current);
    return this.make(this.keywords.get(text, Tok.Identifier));
  }

  // Shared body for a string literal and for the part after an
  // interpolation closes. Stops at the closing quote or at `${`.
  resumeString() {
    const parts = [];
    for (;;) {
      if (this.atEnd()) { return this.errorToken("Unterminated string."); }
      const c = this.advance();
      if (c == "\"") {
        const token = this.make(Tok.String);
        token.text = parts.join("");
        return token;
      }
      if (c == "\n") {
        this.line += 1;
        parts.push(c);
        continue;
      }
      if (c == "\\") {
        if (this.atEnd()) {
          return this.errorToken("Unterminated escape sequence.");
        }
        const escape = this.advance();
        switch (escape) {
          case "n": parts.push("\n");
          case "t": parts.push("\t");
          case "r": parts.push("\r");
          case "0": parts.push(chr(0));
          case "\\": parts.push("\\");
          case "\"": parts.push("\"");
          case "$": parts.push("$");
          case "u": {
            // \u00e9 names a code point with four hex digits. \u{1f600}
            // takes one to six, for the ones that do not fit in four.
            let codePoint = 0;
            let digits = 0;
            const braced = this.match("{");
            let wanted = 4;
            if (braced) { wanted = 6; }
            while (digits < wanted and this.isHexDigit(this.peek())) {
              codePoint = codePoint * 16 + this.hexValue(this.advance());
              digits += 1;
            }
            if (digits == 0 or (!braced and digits < 4)) {
              return this.errorToken(
                "A '\\u' escape needs four hex digits, or braces around " +
                "one to six.");
            }
            if (braced and !this.match("}")) {
              return this.errorToken("Expect '}' to close a '\\u' escape.");
            }
            // 10ffff, and the surrogate range that UTF-8 has no form for.
            if (codePoint > 1114111 or
              (codePoint >= 55296 and codePoint <= 57343)) {
              return this.errorToken(
                "A '\\u' escape must name a code point up to 10ffff, and " +
                "not half of a surrogate pair.");
            }
            parts.push(char(codePoint));
          }
          default:
            return this.errorToken(
              "Unknown escape sequence '\\${escape}'.");
        }
        continue;
      }
      if (c == "$" and this.peek() == "{") {
        this.current += 1;
        // Hand the literal part back and switch to expression scanning.
        // The matching '}' is found by the brace counter in scan().
        this.interpolation.push(0);
        const token = this.make(Tok.StringInterp);
        token.text = parts.join("");
        return token;
      }
      parts.push(c);
    }
  }

  scan() {
    this.skipWhitespace();
    this.start = this.current;
    if (this.atEnd()) { return this.make(Tok.Eof); }

    const c = this.advance();
    if (this.isAlpha(c)) { return this.identifier(); }
    if (this.isDigit(c)) { return this.number(); }

    switch (c) {
      case "(": return this.make(Tok.LeftParen);
      case ")": return this.make(Tok.RightParen);
      case "[": return this.make(Tok.LeftBracket);
      case "]": return this.make(Tok.RightBracket);
      case "{": {
        const depth = this.interpolation.len();
        if (depth > 0) {
          this.interpolation[depth - 1] = this.interpolation[depth - 1] + 1;
        }
        return this.make(Tok.LeftBrace);
      }
      case "}": {
        const depth = this.interpolation.len();
        if (depth > 0) {
          if (this.interpolation[depth - 1] == 0) {
            // This brace closes an interpolation, not a block. Go back to
            // reading string characters.
            this.interpolation.pop();
            return this.resumeString();
          }
          this.interpolation[depth - 1] = this.interpolation[depth - 1] - 1;
        }
        return this.make(Tok.RightBrace);
      }
      case ";": return this.make(Tok.Semicolon);
      case ",": return this.make(Tok.Comma);
      case ".": {
        // "..." marks a rest parameter. A single dot is property access.
        if (this.peek() == "." and this.peekNext() == ".") {
          this.current += 2;
          return this.make(Tok.Ellipsis);
        }
        return this.make(Tok.Dot);
      }
      case ":": return this.make(Tok.Colon);
      case "?": return this.make(Tok.Question);
      case "%": {
        if (this.match("=")) { return this.make(Tok.PercentEqual); }
        return this.make(Tok.Percent);
      }
      case "-": {
        if (this.match(">")) { return this.make(Tok.Arrow); }
        if (this.match("=")) { return this.make(Tok.MinusEqual); }
        return this.make(Tok.Minus);
      }
      case "+": {
        if (this.match("=")) { return this.make(Tok.PlusEqual); }
        return this.make(Tok.Plus);
      }
      case "/": {
        if (this.match("=")) { return this.make(Tok.SlashEqual); }
        return this.make(Tok.Slash);
      }
      case "*": {
        if (this.match("=")) { return this.make(Tok.StarEqual); }
        return this.make(Tok.Star);
      }
      case "&": return this.make(Tok.Ampersand);
      case "|": return this.make(Tok.Pipe);
      case "^": return this.make(Tok.Caret);
      case "~": return this.make(Tok.Tilde);
      case "!": {
        if (this.match("=")) { return this.make(Tok.BangEqual); }
        return this.make(Tok.Bang);
      }
      case "=": {
        if (this.match("=")) { return this.make(Tok.EqualEqual); }
        return this.make(Tok.Equal);
      }
      case "<": {
        if (this.match("<")) { return this.make(Tok.LessLess); }
        if (this.match("=")) { return this.make(Tok.LessEqual); }
        return this.make(Tok.Less);
      }
      case ">": {
        if (this.match(">")) { return this.make(Tok.GreaterGreater); }
        if (this.match("=")) { return this.make(Tok.GreaterEqual); }
        return this.make(Tok.Greater);
      }
      case "\"": return this.resumeString();
      default: return this.errorToken("Unexpected character '${c}'.");
    }
  }
}

// ---------------------------------------------------------------------
// Compiled values
//
// The constant pool holds five kinds of value. A port of the tags in
// src/serialize.cpp; the numbers are part of the file format.

const C_NIL = 0;
const C_FALSE = 1;
const C_TRUE = 2;
const C_NUMBER = 3;
const C_STRING = 4;
const C_FUNCTION = 5;
const C_ENUM = 6;
const C_ENUM_REFERENCE = 7;
// A type, written as its canonical spelling. See src/types.h: the text is
// the whole description, so the compiler never builds a type tree.
const C_TYPE = 8;

class Constant {
  init(tag, value) {
    this.tag = tag;
    this.value = value;
  }

  // Matches valuesEqual() in src/value.cpp, which is what the C++
  // constant pool deduplicates with: numbers compare by value, strings by
  // contents because they are interned, and everything else by identity.
  equals(other) {
    return this.tag == other.tag and this.value == other.value;
  }
}

fun nilConstant() { return Constant(C_NIL, nil); }
fun boolConstant(value) {
  if (value) { return Constant(C_TRUE, nil); }
  return Constant(C_FALSE, nil);
}
fun numberConstant(value) { return Constant(C_NUMBER, value); }
fun stringConstant(value) { return Constant(C_STRING, value); }
fun typeConstantValue(text) { return Constant(C_TYPE, text); }

// The simple type names, in the order src/object.h declares TypeKind.
const SIMPLE_TYPE_NAMES = ["Any", "Nil", "Bool", "Num", "Int", "String",
  "Array", "Map", "Set", "Fun", "Error"];

// Can a literal be measured against this type without running the
// program? Mirrors typeIsStaticallyKnown() in src/types.cpp: everything
// but a class or enum name is settled by the spelling alone, and those
// are bound while the program runs.
fun typeIsStaticallyKnown(text) {
  let bare = text;
  if (bare.ends_with("?")) { bare = bare.sub(0, bare.len() - 1); }
  if (bare.starts_with("[") or bare.starts_with("{") or
    bare.starts_with("fun(") or bare.starts_with("Set[")) {
    return true;
  }
  return SIMPLE_TYPE_NAMES.index_of(bare) >= 0;
}

// Why this constant does not fit this type, or nil when it does or when
// the answer has to wait for the program to run. The wording matches
// typeMatches() in src/types.cpp, because both compilers have to reject
// the same programs and say the same thing about them.
fun whyLiteralMisfits(text, constant) {
  if (constant == nil or !typeIsStaticallyKnown(text)) { return nil; }

  let bare = text;
  const optional = bare.ends_with("?");
  if (optional) {
    if (constant.tag == C_NIL) { return nil; }
    bare = bare.sub(0, bare.len() - 1);
  }

  let fits = false;
  if (bare == "Any") { fits = true; }
  else if (bare == "Nil") { fits = constant.tag == C_NIL; }
  else if (bare == "Bool") {
    fits = constant.tag == C_TRUE or constant.tag == C_FALSE;
  } else if (bare == "Num") { fits = constant.tag == C_NUMBER; }
  else if (bare == "String") { fits = constant.tag == C_STRING; }
  else if (bare == "Int") {
    if (constant.tag == C_NUMBER) {
      const value = constant.value;
      // A whole number, and finite. Int asks about the value, because
      // Red has one number type.
      if (value == floor(value) and value - value == 0) { return nil; }
      return "expected Int, got the number " + str(value);
    }
  }
  // Everything left is a container or a function type, which no literal
  // the compiler can see ever fits.

  if (fits) { return nil; }
  return "expected " + text + ", got " + constantTypeName(constant);
}

// What valueTypeName() in src/value.cpp calls this kind of value.
fun constantTypeName(constant) {
  switch (constant.tag) {
    case C_NIL: return "nil";
    case C_TRUE, C_FALSE: return "bool";
    case C_NUMBER: return "number";
    case C_STRING: return "string";
    default: return "object";
  }
}

// An enum built while compiling and stored whole in the constant pool, so
// that declaring one costs nothing at run time.
class EnumDef {
  init(name) {
    this.name = name;
    this.memberNames = [];
    this.memberValues = [];
    this.seen = {};
  }

  has(name) { return this.seen.has(name); }

  add(name, value) {
    this.seen.set(name, true);
    this.memberNames.push(name);
    this.memberValues.push(value);
  }

  len() { return this.memberNames.len(); }
}

// One compiled function: its code, the constants it names, and the table
// that maps each byte back to a source line. The same fields ObjFunction
// carries in src/object.h, minus the ones only the runtime fills in.
class Proto {
  init() {
    this.name = nil;
    this.arity = 0;
    this.maxArity = 0;
    this.upvalueCount = 0;
    this.slotCount = 0;
    this.hasRest = false;
    this.returnType = "";
    this.paramTypes = [];
    this.paramNames = [];
    this.code = [];
    // Source lines as runs of [line, count] rather than one entry per
    // byte, the way src/chunk.cpp stores them.
    this.lines = [];
    this.constants = [];
  }

  write(byte, line) {
    this.code.push(byte);
    const runs = this.lines.len();
    if (runs > 0 and this.lines[runs - 1][0] == line) {
      this.lines[runs - 1][1] = this.lines[runs - 1][1] + 1;
    } else {
      this.lines.push([line, 1]);
    }
  }

  // Drops everything from newSize onwards, line table included. The
  // constant folder uses this to take back instructions it has replaced.
  truncate(newSize) {
    if (newSize >= this.code.len()) { return; }
    let removing = this.code.len() - newSize;
    while (removing > 0 and this.lines.len() > 0) {
      const last = this.lines[this.lines.len() - 1];
      if (last[1] > removing) {
        last[1] = last[1] - removing;
        removing = 0;
      } else {
        removing -= last[1];
        this.lines.pop();
      }
    }
    while (this.code.len() > newSize) { this.code.pop(); }
  }

  // Appends a constant and gives its index. Identical constants are
  // shared, which keeps the pool small when a loop body mentions the same
  // literal on every pass.
  addConstant(constant) {
    for (let i in range(0, this.constants.len())) {
      if (this.constants[i].equals(constant)) { return i; }
    }
    this.constants.push(constant);
    return this.constants.len() - 1;
  }
}

// ---------------------------------------------------------------------
// The compiled file writer
//
// A port of the writing half of src/serialize.cpp. docs/bytecode.md
// describes the layout.

// The four bytes every compiled file starts with.
const COMPILED_MAGIC = "REDC";

// Splits a value below 2^32 into four bytes, most significant first.
fun fixedBytes(value) {
  return [
    floor(value / 16777216) % 256,
    floor(value / 65536) % 256,
    floor(value / 256) % 256,
    value % 256,
  ];
}

// IEEE 754 binary64, most significant byte first: the same eight bytes
// the C++ writer copies straight out of the double. Red cannot look at a
// number's bits, so they are recovered with exact arithmetic. Every step
// below is a scale by a power of two or a subtraction that cancels, so
// nothing rounds.
fun doubleBytes(value) {
  let sign = 0;
  let biased = 0;
  let frac = 0;

  if (value != value) {
    // Not a number. The payload is the one the usual hardware produces
    // for a quiet NaN, which is what a literal like 0/0 would carry.
    biased = 2047;
    frac = 2251799813685248;
  } else {
    if (value < 0) {
      sign = 1;
      value = -value;
    } else if (value == 0 and str(value) == "-0") {
      sign = 1;
    }

    if (value == 0) {
      biased = 0;
    } else if (value > 1.7976931348623157e308) {
      biased = 2047;
    } else {
      let exponent = 0;
      let mantissa = value;
      while (mantissa >= 2) {
        mantissa = mantissa / 2;
        exponent += 1;
      }
      while (mantissa < 1) {
        mantissa = mantissa * 2;
        exponent -= 1;
      }
      if (exponent >= -1022) {
        biased = exponent + 1023;
        frac = (mantissa - 1) * 4503599627370496;
      } else {
        // Below the smallest normal exponent the fraction is stored at a
        // fixed scale, so the mantissa is shifted down to reach it.
        biased = 0;
        frac = mantissa * 4503599627370496;
        let shift = -1022 - exponent;
        while (shift > 0) {
          frac = frac / 2;
          shift -= 1;
        }
      }
    }
  }

  const carry = floor(frac / 4294967296);
  const high = sign * 2147483648 + biased * 1048576 + carry;
  const low = frac - carry * 4294967296;
  const out = fixedBytes(high);
  for (let b in fixedBytes(low)) { out.push(b); }
  return out;
}

class Writer {
  init() {
    this.parts = [];
    // Enums are shared by identity, so each one is written out once and
    // named by position after that.
    this.enums = [];
  }

  byte(value) { this.parts.push(chr(value)); }

  raw(bytes) {
    const out = [];
    for (let b in bytes) { out.push(chr(b)); }
    this.parts.push(out.join(""));
  }

  // Fixed width, used only in the header, so that the version check keeps
  // working even if the encoding below ever changes.
  fixed(value) { this.raw(fixedBytes(value)); }

  // Seven bits a byte, low group first, with the top bit set while more
  // follow. Nearly every count in a chunk is small, so this is much
  // smaller than four bytes each.
  word(value) {
    while (value >= 128) {
      this.byte(value % 128 + 128);
      value = floor(value / 128);
    }
    this.byte(value);
  }

  number(value) { this.raw(doubleBytes(value)); }

  text(value) {
    this.word(value.len());
    this.parts.push(value);
  }

  result() { return this.parts.join(""); }

  writeFunction(proto) {
    if (proto.name == nil) {
      this.byte(0);
    } else {
      this.byte(1);
      this.text(proto.name);
    }

    this.word(proto.arity);
    this.word(proto.maxArity);
    this.word(proto.upvalueCount);
    this.word(proto.slotCount);
    if (proto.hasRest) { this.byte(1); } else { this.byte(0); }
    this.text(proto.returnType);

    this.word(proto.paramTypes.len());
    for (let type in proto.paramTypes) { this.text(type); }

    this.word(proto.paramNames.len());
    for (let name in proto.paramNames) { this.text(name); }

    this.word(proto.code.len());
    this.raw(proto.code);

    this.word(proto.lines.len());
    for (let run in proto.lines) {
      this.word(run[0]);
      this.word(run[1]);
    }

    this.word(proto.constants.len());
    for (let constant in proto.constants) { this.writeValue(constant); }
  }

  writeValue(constant) {
    switch (constant.tag) {
      case C_NIL, C_FALSE, C_TRUE: this.byte(constant.tag);
      case C_NUMBER: {
        this.byte(C_NUMBER);
        this.number(constant.value);
      }
      case C_STRING: {
        this.byte(C_STRING);
        this.text(constant.value);
      }
      case C_TYPE: {
        this.byte(C_TYPE);
        this.text(constant.value);
      }
      case C_FUNCTION: {
        this.byte(C_FUNCTION);
        this.writeFunction(constant.value);
      }
      case C_ENUM: {
        const seen = this.enums.index_of(constant.value);
        if (seen >= 0) {
          this.byte(C_ENUM_REFERENCE);
          this.word(seen);
        } else {
          this.enums.push(constant.value);
          this.byte(C_ENUM);
          this.text(constant.value.name);
          this.word(constant.value.len());
          for (let i in range(0, constant.value.len())) {
            this.text(constant.value.memberNames[i]);
            this.number(constant.value.memberValues[i]);
          }
        }
      }
      default:
        throw error("cannot write a constant with tag ${constant.tag}",
          nil, "internal");
    }
  }
}

// Turns a finished root function into the bytes of a .redc file.
fun writeCompiled(root) {
  const writer = Writer();
  writer.parts.push(COMPILED_MAGIC);
  writer.fixed(BYTECODE_VERSION);
  writer.writeFunction(root);
  return writer.result();
}

// ---------------------------------------------------------------------
// Compiler state

class Local {
  init(name, depth, isCaptured, isConst) {
    this.name = name;
    this.depth = depth;
    this.isCaptured = isCaptured;
    this.isConst = isConst;
    // What it was declared to be, canonically spelled, or "". Kept so
    // that assigning to it is checked the same way declaring it was.
    this.declaredType = "";
  }
}

class Upvalue {
  init(index, isLocal, declaredType) {
    this.index = index;
    this.isLocal = isLocal;
    // Carried down from the local this captures, so assigning through a
    // closure is checked like assigning directly.
    this.declaredType = declaredType;
  }
}

// Where break and continue jump to for the innermost loop.
class LoopState {
  init(continueTarget, scopeDepth, tryDepth) {
    this.continueTarget = continueTarget;
    this.scopeDepth = scopeDepth;
    // Try blocks open when the loop began. Leaving the loop from inside
    // one has to drop the handlers opened since.
    this.tryDepth = tryDepth;
    this.breakJumps = [];
  }
}

// One per try statement being compiled. Every way out of a try routes
// through its finally block, so each exit records why it is leaving in
// the action slot and what it carries in the pending slot.
class FinallyContext {
  init(actionSlot, pendingSlot, scopeDepth, tryDepth, loopDepth) {
    this.actionSlot = actionSlot;
    this.pendingSlot = pendingSlot;
    this.scopeDepth = scopeDepth;
    this.tryDepth = tryDepth;
    // Loops open when the try began, used to tell whether a break inside
    // the body belongs to a loop outside the try.
    this.loopDepth = loopDepth;
    // Jumps from return, break and continue inside the try body.
    this.jumpsToFinally = [];
  }
}

// One binding of a destructuring pattern.
class PatternBinding {
  init() {
    this.source = PS_INDEX;
    // Position, for PS_INDEX and PS_REST.
    this.index = 0;
    // Field name, for PS_FIELD.
    this.field = "";
    // Name to bind, empty when this binding holds a nested pattern.
    this.name = "";
    this.nested = nil;
  }
}

class Pattern {
  init(isArray) {
    this.isArray = isArray;
    this.bindings = [];
  }
}

// One per function being compiled. They form a stack through `enclosing`,
// which is how capturing an upvalue walks outwards.
class FunctionState {
  init(enclosing, kind, proto) {
    this.enclosing = enclosing;
    this.kind = kind;
    this.proto = proto;
    this.locals = [];
    this.upvalues = [];
    this.scopeDepth = 0;
    this.loops = [];
    // Try blocks currently open in this function.
    this.tryDepth = 0;
    this.finallys = [];
    // Worst case stack use, which is what fills in slotCount.
    this.maxLocals = 0;
    this.maxTemps = 0;
    this.nestDepth = 0;
    // Annotated parameters, as [slot, type]. Checked together once the
    // whole list is parsed, because a default value is compiled where it
    // is written and would otherwise be checked before it ran.
    this.paramChecks = [];
    // One constant per distinct type spelling in this function. A type is
    // a value, so it lives in the constant pool, and writing `Num` twice
    // should not put it there twice.
    this.typeConstants = {};
    // The return type, canonically spelled, or "".
    this.returnType = "";
  }
}

class ClassState {
  init(enclosing) {
    this.enclosing = enclosing;
    this.hasSuperclass = false;
  }
}

// Index of the last occurrence of `needle`, or -1.
fun lastIndexOf(text, needle) {
  let i = text.len() - needle.len();
  while (i >= 0) {
    if (text.sub(i, i + needle.len()) == needle) { return i; }
    i -= 1;
  }
  return -1;
}

// Compiler messages belong on the error stream, the way the C++ compiler
// writes them. Red has no stderr builtin, so the device is opened by
// name, and anywhere that does not have one falls back to stdout.
fun reportLine(text) {
  try {
    const handle = open("/dev/stderr", "a");
    handle.write(text + "\n");
    handle.close();
  } catch (e) {
    print(text);
  }
}

// ---------------------------------------------------------------------
// Compiler
//
// A port of src/compiler.cpp. Single pass, no syntax tree: one token of
// lookahead, Pratt expressions, bytecode straight out.

class Compiler {
  init(source, modulePath, quiet) {
    this.scanner = Scanner(source);
    this.modulePath = modulePath;
    this.quiet = quiet;
    // Both start as an end-of-file token so that anything emitted
    // before the first real token still has a line to record, the way
    // the C++ compiler's default constructed tokens behave.
    this.current = Token(Tok.Eof, "", "", 0, 1);
    this.previous = Token(Tok.Eof, "", "", 0, 1);
    this.hadError = false;
    this.panicMode = false;
    this.state = nil;
    this.classState = nil;
    // Names declared const at module level. Checked while compiling only.
    this.constGlobals = set();
    // Declared types of module level names, so assigning to one is
    // checked as well as declaring it. Only names from this file, which
    // is as far as one pass over one module can see.
    this.globalTypes = {};
    // Where the last known value was pushed, and what it was, so that a
    // literal that cannot fit its annotation is reported while
    // compiling.
    this.lastPush = -1;
    this.lastPushConstant = nil;
    // Set while parsing the callee of `spawn`, so the argument list is
    // left for the spawn form itself to consume. It applies only to the
    // callee's own top level; every construct that opens a bracket clears
    // it with allowCalls() and puts it back afterwards.
    this.suppressCall = false;
    // Offset of the last OP_CONSTANT emitted, or -1 when the last thing
    // emitted was something else. This is what lets the folder recognise
    // two literals sitting next to each other.
    this.lastConstant = -1;
    this.rules = this.buildRules();
  }

  // ---- rule table ----

  buildRules() {
    const rules = [];
    for (let i in range(0, TOKEN_KINDS)) { rules.push([nil, nil, P_NONE]); }
    rules[Tok.LeftParen.value] = [this.grouping, this.call, P_CALL];
    rules[Tok.LeftBrace.value] = [this.mapLiteral, nil, P_NONE];
    rules[Tok.LeftBracket.value] = [this.arrayLiteral, this.index, P_CALL];
    rules[Tok.Dot.value] = [nil, this.dot, P_CALL];
    rules[Tok.Minus.value] = [this.unary, this.binary, P_TERM];
    rules[Tok.Plus.value] = [nil, this.binary, P_TERM];
    rules[Tok.Slash.value] = [nil, this.binary, P_FACTOR];
    rules[Tok.Star.value] = [nil, this.binary, P_FACTOR];
    rules[Tok.Percent.value] = [nil, this.binary, P_FACTOR];
    rules[Tok.Bang.value] = [this.unary, nil, P_NONE];
    rules[Tok.BangEqual.value] = [nil, this.binary, P_EQUALITY];
    rules[Tok.EqualEqual.value] = [nil, this.binary, P_EQUALITY];
    rules[Tok.Greater.value] = [nil, this.binary, P_COMPARISON];
    rules[Tok.GreaterEqual.value] = [nil, this.binary, P_COMPARISON];
    rules[Tok.Less.value] = [nil, this.binary, P_COMPARISON];
    rules[Tok.LessEqual.value] = [nil, this.binary, P_COMPARISON];
    rules[Tok.Identifier.value] = [this.variable, nil, P_NONE];
    rules[Tok.String.value] = [this.stringLiteral, nil, P_NONE];
    rules[Tok.StringInterp.value] = [this.interpolation, nil, P_NONE];
    rules[Tok.Number.value] = [this.number, nil, P_NONE];
    rules[Tok.And.value] = [nil, this.andOp, P_AND];
    rules[Tok.False.value] = [this.literal, nil, P_NONE];
    rules[Tok.Fun.value] = [this.lambda, nil, P_NONE];
    rules[Tok.Nil.value] = [this.literal, nil, P_NONE];
    rules[Tok.Or.value] = [nil, this.orOp, P_OR];
    rules[Tok.Spawn.value] = [this.spawnExpr, nil, P_NONE];
    rules[Tok.Super.value] = [this.superExpr, nil, P_NONE];
    rules[Tok.This.value] = [this.thisExpr, nil, P_NONE];
    rules[Tok.True.value] = [this.literal, nil, P_NONE];
    rules[Tok.Ampersand.value] = [nil, this.binary, P_BIT_AND];
    rules[Tok.Pipe.value] = [nil, this.binary, P_BIT_OR];
    rules[Tok.Caret.value] = [nil, this.binary, P_BIT_XOR];
    rules[Tok.Tilde.value] = [this.unary, nil, P_NONE];
    rules[Tok.LessLess.value] = [nil, this.binary, P_SHIFT];
    rules[Tok.GreaterGreater.value] = [nil, this.binary, P_SHIFT];
    rules[Tok.Is.value] = [nil, this.isExpr, P_COMPARISON];
    return rules;
  }

  ruleOf(type) { return this.rules[type.value]; }

  // ---- token plumbing ----

  advance() {
    this.previous = this.current;
    for (;;) {
      this.current = this.scanner.scan();
      if (this.current.type != Tok.Error) { break; }
      this.errorAtCurrent(this.current.lexeme);
    }
  }

  check(type) { return this.current.type == type; }

  match(type) {
    if (!this.check(type)) { return false; }
    this.advance();
    return true;
  }

  consume(type, message) {
    if (this.current.type == type) {
      this.advance();
      return;
    }
    this.errorAtCurrent(message);
  }

  error(message) { this.errorAt(this.previous, message); }

  errorAtCurrent(message) { this.errorAt(this.current, message); }

  errorAt(token, message) {
    // One report per statement. After the first, everything up to the
    // next statement boundary is noise caused by the first.
    if (this.panicMode) { return; }
    this.panicMode = true;
    this.hadError = true;
    if (this.quiet) { return; }

    let where = "";
    if (token.type == Tok.Eof) {
      where = " at end";
    } else if (token.type != Tok.Error) {
      where = " at '${token.lexeme}'";
    }
    reportLine("[${this.modulePath} line ${token.line}] Error${where}: " +
      message);
  }

  synchronize() {
    this.panicMode = false;
    while (this.current.type != Tok.Eof) {
      if (this.previous.type == Tok.Semicolon) { return; }
      switch (this.current.type) {
        case Tok.Class, Tok.Enum, Tok.Fun, Tok.Let, Tok.Const, Tok.For,
          Tok.If, Tok.While, Tok.Switch, Tok.Return, Tok.Try, Tok.Throw,
          Tok.Import:
          return;
      }
      this.advance();
    }
  }

  // ---- emitting ----

  chunk() { return this.state.proto; }

  // Raises the recorded worst case number of temporaries.
  noteTemps(count) {
    if (count > this.state.maxTemps) { this.state.maxTemps = count; }
  }

  emitByte(byte) {
    // Anything else emitted breaks the run the folder looks for.
    this.lastConstant = -1;
    this.chunk().write(byte, this.previous.line);
  }

  emitOp(op) { this.emitByte(op.value); }

  emitShort(value) {
    this.emitByte(value >> 8 & 255);
    this.emitByte(value & 255);
  }

  emitJump(op) {
    this.emitOp(op);
    this.emitShort(65535);
    return this.chunk().code.len() - 2;
  }

  patchJump(offset) {
    const jump = this.chunk().code.len() - offset - 2;
    if (jump > 65535) { this.error("Too much code to jump over."); }
    this.chunk().code[offset] = jump >> 8 & 255;
    this.chunk().code[offset + 1] = jump & 255;
  }

  emitLoop(loopStart) {
    this.emitOp(Op.Loop);
    const offset = this.chunk().code.len() - loopStart + 2;
    if (offset > 65535) { this.error("Loop body too large."); }
    this.emitShort(offset);
  }

  // `written` says whether the program wrote this return or the compiler
  // is closing a body that ran off its end.
  emitReturn(written) {
    if (this.state.kind == K_INITIALIZER) {
      // An initializer always answers with the instance, whatever the
      // body did, so `let a = Animal("cat")` never yields nil by mistake.
      this.emitOp(Op.GetLocal);
      this.emitByte(0);
    } else {
      this.emitOp(Op.Nil);
      // Running off the end of a function that promised to return
      // something is exactly what an annotation is there to catch, so the
      // implicit nil is checked like any other returned value. Not while
      // compiling though: this instruction is unreachable in a function
      // whose every path returns.
      this.emitTypeCheck2(this.state.returnType, written);
    }
    this.emitOp(Op.Return);
  }

  makeConstant(constant) {
    const index = this.chunk().addConstant(constant);
    if (index > 65535) {
      this.error("Too many constants in one chunk.");
      return 0;
    }
    return index;
  }

  emitConstant(constant) {
    const offset = this.chunk().code.len();
    this.emitOp(Op.Constant);
    this.emitShort(this.makeConstant(constant));
    this.lastConstant = offset;
    this.lastPush = this.chunk().code.len();
    this.lastPushConstant = constant;
  }

  constantAt(offset) {
    const code = this.chunk().code;
    const index = (code[offset + 1] << 8) | code[offset + 2];
    return this.chunk().constants[index];
  }

  // Replaces two adjacent constants and an operator with the answer. Work
  // done here is work the program never repeats, which is the point of
  // compiling ahead of time.
  tryFoldBinary(op, leftOffset, rightOffset) {
    const left = this.constantAt(leftOffset);
    const right = this.constantAt(rightOffset);
    let folded = nil;

    if (left.tag == C_NUMBER and right.tag == C_NUMBER) {
      const a = left.value;
      const b = right.value;
      switch (op) {
        case Tok.Plus: folded = numberConstant(a + b);
        case Tok.Minus: folded = numberConstant(a - b);
        case Tok.Star: folded = numberConstant(a * b);
        case Tok.Slash: {
          // Left alone so that it still reports at run time, where the
          // line and the call stack are available.
          if (b == 0) { return false; }
          folded = numberConstant(a / b);
        }
        case Tok.Percent: {
          if (b == 0) { return false; }
          folded = numberConstant(a % b);
        }
        case Tok.Ampersand: folded = numberConstant(a & b);
        case Tok.Pipe: folded = numberConstant(a | b);
        case Tok.Caret: folded = numberConstant(a ^ b);
        case Tok.LessLess: folded = numberConstant(a << b);
        case Tok.GreaterGreater: folded = numberConstant(a >> b);
        default: return false;
      }
    } else if (op == Tok.Plus and left.tag == C_STRING and
      right.tag == C_STRING) {
      folded = stringConstant(left.value + right.value);
    } else {
      return false;
    }

    this.chunk().truncate(leftOffset);
    this.emitConstant(folded);
    return true;
  }

  identifierConstant(name) { return this.makeConstant(stringConstant(name)); }

  // ---- scopes and variables ----

  beginScope() { this.state.scopeDepth += 1; }

  endScope() {
    this.state.scopeDepth -= 1;
    while (this.state.locals.len() > 0 and
      this.state.locals[this.state.locals.len() - 1].depth >
      this.state.scopeDepth) {
      // A captured local lives on in an upvalue, so it has to move to the
      // heap rather than simply be discarded.
      if (this.state.locals[this.state.locals.len() - 1].isCaptured) {
        this.emitOp(Op.CloseUpvalue);
      } else {
        this.emitOp(Op.Pop);
      }
      this.state.locals.pop();
    }
  }

  addLocal(name, isConst) {
    if (this.state.locals.len() >= MAX_LOCALS) {
      this.error("Too many local variables in function.");
      return;
    }
    this.state.locals.push(Local(name, -1, false, isConst));
    if (this.state.locals.len() > this.state.maxLocals) {
      this.state.maxLocals = this.state.locals.len();
    }
  }

  // Takes the name rather than reading the last token, because patterns
  // declare names long after the token that carried them.
  declareVariable(name, isConst) {
    if (this.state.scopeDepth == 0) { return; }
    let i = this.state.locals.len() - 1;
    while (i >= 0) {
      const local = this.state.locals[i];
      if (local.depth != -1 and local.depth < this.state.scopeDepth) { break; }
      if (local.name == name) {
        this.error("A variable with this name already exists in this scope.");
      }
      i -= 1;
    }
    this.addLocal(name, isConst);
  }

  parseVariable(message, isConst) {
    this.consume(Tok.Identifier, message);
    return this.declareParsedVariable(this.previous.lexeme, isConst);
  }

  // The part of parseVariable that runs once the name has been consumed.
  // A for-in loop has to read the name first to see whether "in" follows.
  declareParsedVariable(name, isConst) {
    this.declareVariable(name, isConst);
    if (this.state.scopeDepth > 0) { return 0; }
    if (isConst) { this.constGlobals.add(name); }
    return this.identifierConstant(name);
  }

  // Declares a hidden slot that user code cannot name. Hidden names carry
  // a space so they can never collide with an identifier.
  addHiddenLocal(name) {
    this.addLocal(name, true);
    // Usable straight away, including at the top level where
    // markInitialized does nothing.
    this.state.locals[this.state.locals.len() - 1].depth =
    this.state.scopeDepth;
    return this.state.locals.len() - 1;
  }

  markInitialized() {
    if (this.state.scopeDepth == 0) { return; }
    this.state.locals[this.state.locals.len() - 1].depth =
    this.state.scopeDepth;
  }

  defineVariable(global, isConst) {
    if (this.state.scopeDepth > 0) {
      this.markInitialized();
      return;
    }
    this.emitOp(Op.DefineGlobal);
    this.emitShort(global);
  }

  resolveLocal(state, name) {
    let i = state.locals.len() - 1;
    while (i >= 0) {
      if (state.locals[i].name == name) {
        if (state.locals[i].depth == -1) {
          this.error("Cannot read a local variable inside its own initializer.");
        }
        return i;
      }
      i -= 1;
    }
    return -1;
  }

  addUpvalue(state, index, isLocal, declaredType) {
    const count = state.proto.upvalueCount;
    for (let i in range(0, count)) {
      if (state.upvalues[i].index == index and
        state.upvalues[i].isLocal == isLocal) {
        return i;
      }
    }
    if (count == MAX_UPVALUES) {
      this.error("Too many closure variables in function.");
      return 0;
    }
    state.upvalues.push(Upvalue(index, isLocal, declaredType));
    state.proto.upvalueCount += 1;
    return count;
  }

  resolveUpvalue(state, name) {
    if (state.enclosing == nil) { return -1; }

    const local = this.resolveLocal(state.enclosing, name);
    if (local != -1) {
      const captured = state.enclosing.locals[local];
      captured.isCaptured = true;
      return this.addUpvalue(state, local, true, captured.declaredType);
    }
    // Not a direct parent local, so look further out. Each level adds one
    // hop, which is what makes deeply nested closures work. The declared
    // type travels with it, so a name annotated three functions out is
    // still checked here.
    const upvalue = this.resolveUpvalue(state.enclosing, name);
    if (upvalue != -1) {
      return this.addUpvalue(state, upvalue, false,
        state.enclosing.upvalues[upvalue].declaredType);
    }
    return -1;
  }

  // Matches a compound assignment operator such as "+=" and gives back
  // the arithmetic opcode that goes with it, or nil.
  matchCompound() {
    if (this.match(Tok.PlusEqual)) { return Op.Add; }
    if (this.match(Tok.MinusEqual)) { return Op.Subtract; }
    if (this.match(Tok.StarEqual)) { return Op.Multiply; }
    if (this.match(Tok.SlashEqual)) { return Op.Divide; }
    if (this.match(Tok.PercentEqual)) { return Op.Modulo; }
    return nil;
  }

  namedVariable(name, canAssign) {
    let getOp = nil;
    let setOp = nil;
    let isConstLocal = false;
    // An annotation is meant to hold for as long as the name does, so an
    // assignment is checked with the type the declaration gave it.
    let declared = "";
    let arg = this.resolveLocal(this.state, name);
    if (arg != -1) {
      getOp = Op.GetLocal;
      setOp = Op.SetLocal;
      isConstLocal = this.state.locals[arg].isConst;
      declared = this.state.locals[arg].declaredType;
    } else {
      arg = this.resolveUpvalue(this.state, name);
      if (arg != -1) {
        getOp = Op.GetUpvalue;
        setOp = Op.SetUpvalue;
        declared = this.state.upvalues[arg].declaredType;
      } else {
        arg = this.identifierConstant(name);
        getOp = Op.GetGlobal;
        setOp = Op.SetGlobal;
        if (this.globalTypes.has(name)) { declared = this.globalTypes[name]; }
      }
    }

    const isGlobal = getOp == Op.GetGlobal;
    const isConstBinding =
    isConstLocal or (isGlobal and this.constGlobals.has(name));

    const compound = this.matchCompound2(canAssign);
    if (compound != nil) {
      if (isConstBinding) { this.error("Cannot assign to a const binding."); }
      // Read the current value, combine it, and write it back.
      this.emitOp(getOp);
      if (isGlobal) { this.emitShort(arg); } else { this.emitByte(arg); }
      this.expression();
      this.emitOp(compound);
      this.emitTypeCheck(declared);
      this.emitOp(setOp);
      if (isGlobal) { this.emitShort(arg); } else { this.emitByte(arg); }
    } else if (canAssign and this.match(Tok.Equal)) {
      if (isConstBinding) { this.error("Cannot assign to a const binding."); }
      this.expression();
      this.emitTypeCheck(declared);
      this.emitOp(setOp);
      if (isGlobal) { this.emitShort(arg); } else { this.emitByte(arg); }
    } else {
      this.emitOp(getOp);
      if (isGlobal) { this.emitShort(arg); } else { this.emitByte(arg); }
    }
  }

  // matchCompound only when assignment is allowed here, so that the
  // operator is not eaten in a position that cannot assign.
  matchCompound2(canAssign) {
    if (!canAssign) { return nil; }
    return this.matchCompound();
  }

  // Consumes an optional `: Type` annotation and gives back its canonical
  // spelling, or "" when there was no annotation.
  typeAnnotation() {
    if (!this.match(Tok.Colon)) { return ""; }
    return this.typeText();
  }

  // Reads a type, with the ':' or '->' already consumed. The canonical
  // spelling is the whole of what a type is as far as the compiler and
  // the compiled file are concerned; src/types.cpp reads it back.
  typeText() {
    let text = "";

    if (this.match(Tok.LeftBracket)) {
      text = "[" + this.typeText() + "]";
      this.consume(Tok.RightBracket, "Expect ']' after an element type.");
    } else if (this.match(Tok.LeftBrace)) {
      const key = this.typeText();
      this.consume(Tok.Colon,
        "Expect ':' between a key type and a value type.");
      text = "{" + key + ": " + this.typeText() + "}";
      this.consume(Tok.RightBrace, "Expect '}' after a map type.");
    } else if (this.match(Tok.Fun)) {
      text = "fun(";
      this.consume(Tok.LeftParen, "Expect '(' after 'fun' in a type.");
      if (!this.check(Tok.RightParen)) {
        let first = true;
        for (;;) {
          if (!first) { text += ", "; }
          first = false;
          text += this.typeText();
          if (!this.match(Tok.Comma)) { break; }
        }
      }
      this.consume(Tok.RightParen, "Expect ')' after parameter types.");
      // The return type is always part of the canonical spelling, so a
      // function type written without one returns Any.
      text += ") -> ";
      if (this.match(Tok.Arrow)) { text += this.typeText(); }
      else { text += "Any"; }
    } else {
      this.consume(Tok.Identifier, "Expect a type.");
      text = this.previous.lexeme;
      // Set is the one named type that takes a parameter. Arrays and maps
      // have brackets of their own.
      if (text == "Set" and this.match(Tok.LeftBracket)) {
        text += "[" + this.typeText() + "]";
        this.consume(Tok.RightBracket, "Expect ']' after an element type.");
      }
    }

    // `T?` admits nil as well, and writing it twice says nothing more
    // than writing it once.
    let optional = false;
    while (this.match(Tok.Question)) { optional = true; }
    if (optional and text != "Any" and text[text.len() - 1] != "?") {
      text += "?";
    }
    return text;
  }

  // Records what a name was declared to be, for the assignments that
  // come later.
  noteDeclaredType(name, text) {
    if (text == "") { return; }
    if (this.state.scopeDepth > 0) {
      if (this.state.locals.len() > 0) {
        this.state.locals[this.state.locals.len() - 1].declaredType = text;
      }
      return;
    }
    this.globalTypes[name] = text;
  }

  // The constant holding this type, adding it the first time the
  // spelling turns up in this function. Gives -1 when there is nothing
  // to name.
  typeConstant(text) {
    if (text == "") { return -1; }
    const seen = this.state.typeConstants;
    if (seen.has(text)) { return seen[text]; }
    const index = this.makeConstant(typeConstantValue(text));
    seen[text] = index;
    return index;
  }

  // Emits a check on the value at the top of the stack, which stays
  // where it is.
  emitTypeCheck(text) { this.emitTypeCheck2(text, true); }

  // `reportLiteral` is false where the value is one the compiler put
  // there rather than one the program wrote, which is the implicit nil
  // at the end of a function. That instruction is often unreachable, so
  // failing it while compiling would reject working programs.
  emitTypeCheck2(text, reportLiteral) {
    // Any is what an unannotated name already means, so it needs no
    // check and no constant.
    if (text == "" or text == "Any") { return; }
    const constant = this.typeConstant(text);

    // A literal that cannot fit is a mistake in the program rather than
    // something that might work on some run, so it is reported now. See
    // typeIsStaticallyKnown() in src/types.cpp for where the line falls.
    if (reportLiteral and this.lastPush == this.chunk().code.len()) {
      const reason = whyLiteralMisfits(text, this.lastPushConstant);
      if (reason != nil) { this.error(reason + "."); }
    }

    this.emitOp(Op.CheckType);
    this.emitShort(constant);
  }

  // `x is T`. The right side is a type, read the same way an annotation
  // is, so `x is [Num]` can be written as well as `x is Point`.
  isExpr(canAssign) {
    // Unlike an annotation this always needs the type on the stack, so
    // even Any goes into the pool.
    const constant = this.typeConstant(this.typeText());
    if (constant < 0) { return; }
    this.emitOp(Op.Constant);
    this.emitShort(constant);
    this.emitOp(Op.Is);
    this.noteTemps(2);
  }

  // Clears the spawn guard for one nested construct and gives back the
  // previous setting, which restoreCalls() puts back. Every construct
  // that opens a bracket and parses expressions inside it needs this, or
  // a call written inside `spawn f(...)` would be suppressed too.
  allowCalls() {
    const saved = this.suppressCall;
    this.suppressCall = false;
    return saved;
  }

  restoreCalls(saved) { this.suppressCall = saved; }

  // ---- declarations ----

  declaration() {
    if (this.match(Tok.Class)) {
      this.classDeclaration();
    } else if (this.match(Tok.Enum)) {
      this.enumDeclaration();
    } else if (this.match(Tok.Fun)) {
      this.funDeclaration();
    } else if (this.match(Tok.Let)) {
      this.varDeclaration(false);
    } else if (this.match(Tok.Const)) {
      this.varDeclaration(true);
    } else if (this.match(Tok.Import)) {
      this.importDeclaration();
    } else {
      this.statement();
    }
    if (this.panicMode) { this.synchronize(); }
  }

  parsePattern() {
    if (this.match(Tok.LeftBracket)) {
      const pattern = Pattern(true);
      let index = 0;
      if (!this.check(Tok.RightBracket)) {
        for (;;) {
          if (this.check(Tok.RightBracket)) { break; }
          const binding = PatternBinding();
          if (this.match(Tok.Ellipsis)) {
            // A rest binding takes everything left, so nothing follows it.
            binding.source = PS_REST;
            binding.index = index;
            this.consume(Tok.Identifier, "Expect a name after '...'.");
            binding.name = this.previous.lexeme;
            pattern.bindings.push(binding);
            break;
          }
          binding.source = PS_INDEX;
          binding.index = index;
          index += 1;
          if (this.check(Tok.LeftBracket) or this.check(Tok.LeftBrace)) {
            binding.nested = this.parsePattern();
          } else {
            this.consume(Tok.Identifier, "Expect a name in a pattern.");
            binding.name = this.previous.lexeme;
          }
          pattern.bindings.push(binding);
          if (!this.match(Tok.Comma)) { break; }
        }
      }
      this.consume(Tok.RightBracket, "Expect ']' after a pattern.");
      return pattern;
    }

    this.consume(Tok.LeftBrace, "Expect '[' or '{' to start a pattern.");
    const pattern = Pattern(false);
    if (!this.check(Tok.RightBrace)) {
      for (;;) {
        if (this.check(Tok.RightBrace)) { break; }
        const binding = PatternBinding();
        binding.source = PS_FIELD;
        this.consume(Tok.Identifier, "Expect a field name in a pattern.");
        binding.field = this.previous.lexeme;
        binding.name = binding.field;
        if (this.match(Tok.Colon)) {
          if (this.check(Tok.LeftBracket) or this.check(Tok.LeftBrace)) {
            binding.nested = this.parsePattern();
            binding.name = "";
          } else {
            this.consume(Tok.Identifier, "Expect a name after ':'.");
            binding.name = this.previous.lexeme;
          }
        }
        pattern.bindings.push(binding);
        if (!this.match(Tok.Comma)) { break; }
      }
    }
    this.consume(Tok.RightBrace, "Expect '}' after a pattern.");
    return pattern;
  }

  // Emits the bindings of a pattern, reading from the local at
  // subjectSlot. Gives back how many hidden slots it added.
  emitPattern(pattern, subjectSlot, isConst) {
    let hidden = 0;
    for (let binding in pattern.bindings) {
      this.emitOp(Op.GetLocal);
      this.emitByte(subjectSlot);
      switch (binding.source) {
        case PS_INDEX: {
          this.emitOp(Op.DestructureIndex);
          this.emitShort(binding.index);
        }
        case PS_REST: {
          this.emitOp(Op.DestructureRest);
          this.emitShort(binding.index);
        }
        case PS_FIELD: {
          this.emitOp(Op.DestructureField);
          this.emitShort(this.identifierConstant(binding.field));
        }
      }

      if (binding.nested != nil) {
        // The extracted value becomes the subject of the inner pattern.
        const nestedSlot = this.addHiddenLocal("  nested");
        hidden += 1;
        hidden += this.emitPattern(binding.nested, nestedSlot, isConst);
        continue;
      }

      const global = this.declareParsedVariable(binding.name, isConst);
      this.defineVariable(global, isConst);
    }
    return hidden;
  }

  destructuringDeclaration(isConst) {
    const pattern = this.parsePattern();
    this.consume(Tok.Equal, "A destructuring declaration needs a value.");
    this.expression();
    this.consume(Tok.Semicolon, "Expect ';' after a variable declaration.");

    const subjectSlot = this.addHiddenLocal("  subject");
    const hidden = this.emitPattern(pattern, subjectSlot, isConst) + 1;

    if (this.state.scopeDepth == 0) {
      // At the top level the bindings became globals and popped
      // themselves, so only the hidden slots are left to clear.
      for (let i in range(0, hidden)) {
        this.emitOp(Op.Pop);
        this.state.locals.pop();
      }
    }
    // Inside a scope the hidden slots are ordinary locals, and the
    // closing brace pops them along with everything else.
  }

  varDeclaration(isConst) {
    if (this.check(Tok.LeftBracket) or this.check(Tok.LeftBrace)) {
      this.destructuringDeclaration(isConst);
      return;
    }
    const global = this.parseVariable("Expect a variable name.", isConst);
    const name = this.previous.lexeme;
    const declared = this.typeAnnotation();
    this.noteDeclaredType(name, declared);

    if (this.match(Tok.Equal)) {
      this.expression();
      this.emitTypeCheck(declared);
    } else if (isConst) {
      this.error("A const binding must have an initializer.");
      this.emitOp(Op.Nil);
    } else {
      this.emitOp(Op.Nil);
    }
    this.consume(Tok.Semicolon, "Expect ';' after a variable declaration.");
    this.defineVariable(global, isConst);
  }

  funDeclaration() {
    const global = this.parseVariable("Expect a function name.", false);
    const name = this.previous.lexeme;
    // Marked before the body is compiled so a function can call itself.
    this.markInitialized();
    this.function(K_FUNCTION, name);
    this.defineVariable(global, false);
  }

  importDeclaration() {
    this.consume(Tok.String, "Expect a module path string after 'import'.");
    const path = this.previous.text;
    const pathConstant = this.makeConstant(stringConstant(path));

    let binding = "";
    if (this.match(Tok.As)) {
      this.consume(Tok.Identifier, "Expect a name after 'as'.");
      binding = this.previous.lexeme;
    } else {
      // Default the binding to the file stem, so `import "util.red";`
      // binds the name `util`.
      const slash = lastIndexOf(path, "/");
      if (slash < 0) { binding = path; } else { binding = path.sub(slash + 1); }
      const dot = lastIndexOf(binding, ".");
      if (dot >= 0) { binding = binding.sub(0, dot); }
    }
    this.consume(Tok.Semicolon, "Expect ';' after an import.");

    this.emitOp(Op.Import);
    this.emitShort(pathConstant);

    if (this.state.scopeDepth > 0) {
      this.addLocal(binding, true);
      this.markInitialized();
    } else {
      const global = this.identifierConstant(binding);
      this.constGlobals.add(binding);
      this.emitOp(Op.DefineGlobal);
      this.emitShort(global);
    }
  }

  // enum Colour { Red, Green, Blue } or enum Op { Add = 1, Sub }
  //
  // The whole enum is built while compiling and stored as a single
  // constant, so declaring one costs nothing at run time.
  enumDeclaration() {
    this.consume(Tok.Identifier, "Expect an enum name.");
    const enumName = this.previous.lexeme;
    const nameConstant = this.declareParsedVariable(enumName, true);

    const enumeration = EnumDef(enumName);

    this.consume(Tok.LeftBrace, "Expect '{' before enum members.");
    let nextValue = 0;
    while (!this.check(Tok.RightBrace) and !this.check(Tok.Eof)) {
      this.consume(Tok.Identifier, "Expect an enum member name.");
      const memberName = this.previous.lexeme;
      let value = nextValue;
      if (this.match(Tok.Equal)) {
        const negative = this.match(Tok.Minus);
        this.consume(Tok.Number, "Expect a number after '=' in an enum.");
        if (negative) { value = -this.previous.number; }
        else { value = this.previous.number; }
      }
      nextValue = value + 1;

      if (enumeration.has(memberName)) {
        this.error("Duplicate enum member '" + memberName + "'.");
      } else {
        enumeration.add(memberName, value);
      }
      if (!this.match(Tok.Comma)) { break; }
    }
    this.consume(Tok.RightBrace, "Expect '}' after enum members.");

    if (enumeration.len() == 0) {
      this.error("An enum needs at least one member.");
    }

    this.emitOp(Op.Constant);
    this.emitShort(this.makeConstant(Constant(C_ENUM, enumeration)));

    if (this.state.scopeDepth == 0) { this.constGlobals.add(enumName); }
    this.defineVariable(nameConstant, true);
  }

  classDeclaration() {
    this.consume(Tok.Identifier, "Expect a class name.");
    const className = this.previous.lexeme;
    const nameConstant = this.identifierConstant(className);
    this.declareVariable(className, false);

    this.emitOp(Op.Class);
    this.emitShort(nameConstant);
    this.defineVariable(nameConstant, false);

    const classState = ClassState(this.classState);
    this.classState = classState;

    if (this.match(Tok.Less)) {
      this.consume(Tok.Identifier, "Expect a superclass name.");
      this.variable(false);
      if (className == this.previous.lexeme) {
        this.error("A class cannot inherit from itself.");
      }
      // `super` is resolved as an upvalue, so it needs a scope of its own
      // that outlives the method bodies that capture it.
      this.beginScope();
      this.addLocal("super", true);
      this.markInitialized();

      this.namedVariable(className, false);
      this.emitOp(Op.Inherit);
      classState.hasSuperclass = true;
    }

    this.namedVariable(className, false);
    this.consume(Tok.LeftBrace, "Expect '{' before a class body.");
    while (!this.check(Tok.RightBrace) and !this.check(Tok.Eof)) {
      this.method();
    }
    this.consume(Tok.RightBrace, "Expect '}' after a class body.");
    this.emitOp(Op.Pop);

    if (classState.hasSuperclass) { this.endScope(); }
    this.classState = classState.enclosing;
  }

  method() {
    this.consume(Tok.Identifier, "Expect a method name.");
    const name = this.previous.lexeme;
    const constant = this.identifierConstant(name);
    let kind = K_METHOD;
    if (name == "init") { kind = K_INITIALIZER; }
    this.function(kind, name);
    this.emitOp(Op.Method);
    this.emitShort(constant);
  }

  function(kind, name) {
    const savedCalls = this.allowCalls();
    const state = FunctionState(this.state, kind, Proto());
    if (name != "") { state.proto.name = name; }

    // Slot zero holds the receiver for methods and the function itself
    // otherwise. Naming it makes `this` resolve like any other local.
    let slotZero = "this";
    if (kind == K_FUNCTION or kind == K_SCRIPT) { slotZero = ""; }
    state.locals.push(Local(slotZero, 0, false, true));
    this.state = state;
    this.beginScope();

    this.consume(Tok.LeftParen, "Expect '(' after a function name.");
    let seenOptional = false;
    if (!this.check(Tok.RightParen)) {
      for (;;) {
        if (this.match(Tok.Ellipsis)) {
          // A rest parameter gathers whatever is left, so nothing can
          // follow it.
          const restConstant =
          this.parseVariable("Expect a name after '...'.", false);
          state.proto.paramNames.push(this.previous.lexeme);
          state.proto.paramTypes.push("");
          this.defineVariable(restConstant, false);
          state.proto.hasRest = true;
          break;
        }

        state.proto.maxArity += 1;
        if (state.proto.maxArity > 255) {
          this.errorAtCurrent("Cannot have more than 255 parameters.");
        }
        const constant = this.parseVariable("Expect a parameter name.", false);
        const paramName = this.previous.lexeme;
        state.proto.paramNames.push(paramName);
        const annotation = this.typeAnnotation();
        this.noteDeclaredType(paramName, annotation);
        state.proto.paramTypes.push(annotation);
        if (annotation != "" and annotation != "Any") {
          // Slot zero is the receiver, so the first parameter is slot one.
          state.paramChecks.push([state.proto.maxArity, annotation]);
        }
        this.defineVariable(constant, false);

        if (this.match(Tok.Equal)) {
          seenOptional = true;
          // The default is compiled here, which puts it at the top of the
          // body, and it is skipped when the call supplied this argument.
          // Parameters declared earlier are already in scope, so a
          // default can refer to them.
          const index = state.proto.maxArity - 1;
          this.emitOp(Op.JumpIfArg);
          this.emitByte(index);
          this.emitShort(65535);
          const suppliedJump = this.chunk().code.len() - 2;
          this.expression();
          this.emitOp(Op.SetLocal);
          this.emitByte(index + 1);
          this.emitOp(Op.Pop);
          this.patchJump(suppliedJump);
        } else if (seenOptional) {
          this.error("A required parameter cannot follow one with a default.");
        } else {
          state.proto.arity += 1;
        }
        if (!this.match(Tok.Comma)) { break; }
      }
    }
    this.consume(Tok.RightParen, "Expect ')' after parameters.");

    if (this.match(Tok.Arrow)) {
      state.returnType = this.typeText();
      state.proto.returnType = state.returnType;
    }

    // Every annotated parameter is checked here, after the defaults, so
    // that a default value is checked like anything else arriving in
    // that slot.
    for (let check in state.paramChecks) {
      this.emitOp(Op.CheckLocal);
      this.emitByte(check[0]);
      this.emitShort(this.typeConstant(check[1]));
    }

    this.consume(Tok.LeftBrace, "Expect '{' before a function body.");
    this.block();

    this.emitReturn(false);
    state.proto.slotCount = state.maxLocals + state.maxTemps + 8;
    this.state = state.enclosing;

    this.emitOp(Op.Closure);
    this.emitShort(this.makeConstant(Constant(C_FUNCTION, state.proto)));
    for (let i in range(0, state.proto.upvalueCount)) {
      if (state.upvalues[i].isLocal) { this.emitByte(1); } else { this.emitByte(0); }
      this.emitByte(state.upvalues[i].index);
    }
    this.restoreCalls(savedCalls);
  }

  // ---- statements ----

  statement() {
    if (this.match(Tok.If)) {
      this.ifStatement();
    } else if (this.match(Tok.While)) {
      this.whileStatement();
    } else if (this.match(Tok.For)) {
      this.forStatement();
    } else if (this.match(Tok.Switch)) {
      this.switchStatement();
    } else if (this.match(Tok.Return)) {
      this.returnStatement();
    } else if (this.match(Tok.Break)) {
      this.breakStatement();
    } else if (this.match(Tok.Continue)) {
      this.continueStatement();
    } else if (this.match(Tok.Try)) {
      this.tryStatement();
    } else if (this.match(Tok.Throw)) {
      this.throwStatement();
    } else if (this.match(Tok.LeftBrace)) {
      this.beginScope();
      this.block();
      this.endScope();
    } else {
      this.expressionStatement();
    }
  }

  block() {
    while (!this.check(Tok.RightBrace) and !this.check(Tok.Eof)) {
      this.declaration();
    }
    this.consume(Tok.RightBrace, "Expect '}' after a block.");
  }

  expressionStatement() {
    this.expression();
    this.consume(Tok.Semicolon, "Expect ';' after an expression.");
    this.emitOp(Op.Pop);
  }

  ifStatement() {
    this.consume(Tok.LeftParen, "Expect '(' after 'if'.");
    this.expression();
    this.consume(Tok.RightParen, "Expect ')' after a condition.");

    const thenJump = this.emitJump(Op.JumpIfFalse);
    this.emitOp(Op.Pop);
    this.statement();
    const elseJump = this.emitJump(Op.Jump);

    this.patchJump(thenJump);
    this.emitOp(Op.Pop);
    if (this.match(Tok.Else)) { this.statement(); }
    this.patchJump(elseJump);
  }

  whileStatement() {
    const loopStart = this.chunk().code.len();
    this.state.loops.push(
      LoopState(loopStart, this.state.scopeDepth, this.state.tryDepth));

    this.consume(Tok.LeftParen, "Expect '(' after 'while'.");
    this.expression();
    this.consume(Tok.RightParen, "Expect ')' after a condition.");

    const exitJump = this.emitJump(Op.JumpIfFalse);
    this.emitOp(Op.Pop);
    this.statement();
    this.emitLoop(loopStart);

    this.patchJump(exitJump);
    this.emitOp(Op.Pop);

    for (let jump in this.state.loops[this.state.loops.len() - 1].breakJumps) {
      this.patchJump(jump);
    }
    this.state.loops.pop();
  }

  forStatement() {
    this.beginScope();
    this.consume(Tok.LeftParen, "Expect '(' after 'for'.");

    if (this.match(Tok.Semicolon)) {
      // No initializer.
    } else if (this.match(Tok.Let)) {
      // A pattern here can only belong to a for-in loop.
      if (this.check(Tok.LeftBracket) or this.check(Tok.LeftBrace)) {
        const pattern = this.parsePattern();
        this.consume(Tok.In, "Expect 'in' after a for-in pattern.");
        this.forInStatement("", pattern);
        this.endScope();
        return;
      }
      // The name has to be read before it is clear which kind of loop
      // this is, because "in" only shows up after it.
      this.consume(Tok.Identifier, "Expect a variable name.");
      const name = this.previous.lexeme;
      if (this.match(Tok.In)) {
        this.forInStatement(name, nil);
        this.endScope();
        return;
      }
      const global = this.declareParsedVariable(name, false);
      const declared = this.typeAnnotation();
      this.noteDeclaredType(name, declared);
      if (this.match(Tok.Equal)) {
        this.expression();
        this.emitTypeCheck(declared);
      } else {
        this.emitOp(Op.Nil);
      }
      this.consume(Tok.Semicolon, "Expect ';' after a variable declaration.");
      this.defineVariable(global, false);
    } else {
      this.expressionStatement();
    }

    let loopStart = this.chunk().code.len();
    let exitJump = -1;
    if (!this.match(Tok.Semicolon)) {
      this.expression();
      this.consume(Tok.Semicolon, "Expect ';' after a loop condition.");
      exitJump = this.emitJump(Op.JumpIfFalse);
      this.emitOp(Op.Pop);
    }

    // The increment is compiled before the body but has to run after it,
    // so it is jumped over on the way in and jumped back to on the way
    // out.
    let continueTarget = loopStart;
    if (!this.match(Tok.RightParen)) {
      const bodyJump = this.emitJump(Op.Jump);
      const incrementStart = this.chunk().code.len();
      this.expression();
      this.emitOp(Op.Pop);
      this.consume(Tok.RightParen, "Expect ')' after for clauses.");
      this.emitLoop(loopStart);
      loopStart = incrementStart;
      continueTarget = incrementStart;
      this.patchJump(bodyJump);
    }

    this.state.loops.push(
      LoopState(continueTarget, this.state.scopeDepth, this.state.tryDepth));
    this.statement();
    this.emitLoop(loopStart);

    if (exitJump != -1) {
      this.patchJump(exitJump);
      this.emitOp(Op.Pop);
    }
    for (let jump in this.state.loops[this.state.loops.len() - 1].breakJumps) {
      this.patchJump(jump);
    }
    this.state.loops.pop();
    this.endScope();
  }

  // Drops the try handlers opened inside the loop. Leaving a try block by
  // jumping out of it still has to close it, or the handler stays live
  // and a later throw lands in dead code.
  closeLoopHandlers() {
    const loop = this.state.loops[this.state.loops.len() - 1];
    let i = this.state.tryDepth;
    while (i > loop.tryDepth) {
      this.emitOp(Op.TryEnd);
      i -= 1;
    }
  }

  // for (let x in subject) walks an array, a map's keys, a set, or a
  // string's characters. Two hidden locals hold the sequence and the
  // position.
  forInStatement(name, pattern) {
    this.expression();
    this.consume(Tok.RightParen, "Expect ')' after a for-in subject.");
    this.emitOp(Op.IterPrep);

    // The hidden names contain a space, so no program can reach them.
    this.addLocal("  seq", true);
    this.markInitialized();
    const seqSlot = this.state.locals.len() - 1;

    this.emitConstant(numberConstant(0));
    this.addLocal("  idx", true);
    this.markInitialized();
    const idxSlot = this.state.locals.len() - 1;

    const loopStart = this.chunk().code.len();
    this.emitOp(Op.IterNext);
    this.emitByte(seqSlot);
    this.emitByte(idxSlot);
    this.emitShort(65535);
    const exitJump = this.chunk().code.len() - 2;

    this.state.loops.push(
      LoopState(loopStart, this.state.scopeDepth, this.state.tryDepth));

    // ITER_NEXT leaves the element on top of the stack, which is exactly
    // the slot the loop variable occupies.
    this.beginScope();
    if (pattern != nil) {
      // With a pattern the element goes to a hidden slot and the pattern
      // binds from there.
      const itemSlot = this.addHiddenLocal("  item");
      this.emitPattern(pattern, itemSlot, false);
    } else {
      this.addLocal(name, false);
      this.markInitialized();
    }
    this.statement();
    this.endScope();

    this.emitLoop(loopStart);
    this.patchJump(exitJump);
    for (let jump in this.state.loops[this.state.loops.len() - 1].breakJumps) {
      this.patchJump(jump);
    }
    this.state.loops.pop();
  }

  // Statements belonging to one case, up to the next case or the closing
  // brace. There is no fall through, so no break is needed to end a case.
  caseBody() {
    this.beginScope();
    while (!this.check(Tok.Case) and !this.check(Tok.Default) and
      !this.check(Tok.RightBrace) and !this.check(Tok.Eof)) {
      this.declaration();
    }
    this.endScope();
  }

  switchStatement() {
    this.consume(Tok.LeftParen, "Expect '(' after 'switch'.");
    this.expression();
    this.consume(Tok.RightParen, "Expect ')' after a switch value.");
    this.consume(Tok.LeftBrace, "Expect '{' before switch cases.");

    this.beginScope();
    // The subject is kept in a hidden local so that each case can compare
    // against it without evaluating it again.
    this.addLocal("  switch", true);
    this.markInitialized();
    const valueSlot = this.state.locals.len() - 1;

    const endJumps = [];
    let sawDefault = false;

    while (!this.check(Tok.RightBrace) and !this.check(Tok.Eof)) {
      if (this.match(Tok.Case)) {
        if (sawDefault) { this.error("A case cannot come after the default clause."); }

        const matchJumps = [];
        for (;;) {
          this.emitOp(Op.GetLocal);
          this.emitByte(valueSlot);
          this.expression();
          this.emitOp(Op.Equal);
          matchJumps.push(this.emitJump(Op.JumpIfTrue));
          // This test failed, so drop its result and try the next value.
          this.emitOp(Op.Pop);
          if (!this.match(Tok.Comma)) { break; }
        }
        this.consume(Tok.Colon, "Expect ':' after case values.");

        // Every value failed, so skip the body.
        const skipJump = this.emitJump(Op.Jump);
        for (let jump in matchJumps) { this.patchJump(jump); }
        // Exactly one test left a true behind. Drop it.
        this.emitOp(Op.Pop);
        this.caseBody();
        endJumps.push(this.emitJump(Op.Jump));
        this.patchJump(skipJump);
      } else if (this.match(Tok.Default)) {
        if (sawDefault) { this.error("A switch can only have one default clause."); }
        sawDefault = true;
        this.consume(Tok.Colon, "Expect ':' after 'default'.");
        this.caseBody();
        endJumps.push(this.emitJump(Op.Jump));
      } else {
        this.errorAtCurrent("Expect 'case' or 'default' in a switch body.");
        break;
      }
    }

    this.consume(Tok.RightBrace, "Expect '}' after switch cases.");
    for (let jump in endJumps) { this.patchJump(jump); }
    this.endScope();
  }

  popLocalsTo(targetDepth) {
    let i = this.state.locals.len() - 1;
    while (i >= 0) {
      if (this.state.locals[i].depth <= targetDepth) { break; }
      if (this.state.locals[i].isCaptured) {
        this.emitOp(Op.CloseUpvalue);
      } else {
        this.emitOp(Op.Pop);
      }
      i -= 1;
    }
  }

  // Is the innermost try inside the loop that a break would leave? A try
  // wrapped around the whole loop is not in the way.
  breakRunsFinally() {
    if (this.state.finallys.len() == 0) { return false; }
    const context = this.state.finallys[this.state.finallys.len() - 1];
    return context.loopDepth == this.state.loops.len();
  }

  // Leaves the innermost try through its finally block. For a return the
  // value is already on the stack.
  exitThroughFinally(action, carriesValue) {
    const context = this.state.finallys[this.state.finallys.len() - 1];
    if (carriesValue) {
      this.emitOp(Op.SetLocal);
      this.emitByte(context.pendingSlot);
      this.emitOp(Op.Pop);
    }
    this.emitConstant(numberConstant(action));
    this.emitOp(Op.SetLocal);
    this.emitByte(context.actionSlot);
    this.emitOp(Op.Pop);

    // Close the try's own handler and drop anything its body declared, so
    // the finally block starts from the depth it expects.
    let i = this.state.tryDepth;
    while (i > context.tryDepth) {
      this.emitOp(Op.TryEnd);
      i -= 1;
    }
    this.popLocalsTo(context.scopeDepth);
    context.jumpsToFinally.push(this.emitJump(Op.Jump));
  }

  // Runs straight after the finally body. The action slot says what the
  // exit that reached here was trying to do, and this carries it out. The
  // context has already been popped, so a return or a break here routes
  // through an enclosing finally if there is one.
  emitFinallyDispatch(context) {
    for (let action in [F_RETHROW, F_RETURN, F_BREAK, F_CONTINUE]) {
      const needsLoop = action == F_BREAK or action == F_CONTINUE;
      if (needsLoop and this.state.loops.len() == 0) { continue; }

      this.emitOp(Op.GetLocal);
      this.emitByte(context.actionSlot);
      this.emitConstant(numberConstant(action));
      this.emitOp(Op.Equal);
      const skip = this.emitJump(Op.JumpIfFalse);
      this.emitOp(Op.Pop);

      if (action == F_RETHROW) {
        this.emitOp(Op.GetLocal);
        this.emitByte(context.pendingSlot);
        this.emitOp(Op.Throw);
      } else if (action == F_RETURN) {
        this.emitOp(Op.GetLocal);
        this.emitByte(context.pendingSlot);
        if (this.state.finallys.len() == 0) {
          this.emitOp(Op.Return);
        } else {
          this.exitThroughFinally(F_RETURN, true);
        }
      } else if (action == F_BREAK) {
        if (this.breakRunsFinally()) {
          this.exitThroughFinally(F_BREAK, false);
        } else {
          this.closeLoopHandlers();
          const loop = this.state.loops[this.state.loops.len() - 1];
          this.popLocalsTo(loop.scopeDepth);
          loop.breakJumps.push(this.emitJump(Op.Jump));
        }
      } else {
        if (this.breakRunsFinally()) {
          this.exitThroughFinally(F_CONTINUE, false);
        } else {
          this.closeLoopHandlers();
          const loop = this.state.loops[this.state.loops.len() - 1];
          this.popLocalsTo(loop.scopeDepth);
          this.emitLoop(loop.continueTarget);
        }
      }

      this.patchJump(skip);
      this.emitOp(Op.Pop);
    }
  }

  breakStatement() {
    if (this.state.loops.len() == 0) {
      this.error("Cannot use 'break' outside a loop.");
      return;
    }
    this.consume(Tok.Semicolon, "Expect ';' after 'break'.");
    if (this.breakRunsFinally()) {
      this.exitThroughFinally(F_BREAK, false);
      return;
    }
    this.closeLoopHandlers();
    const loop = this.state.loops[this.state.loops.len() - 1];
    this.popLocalsTo(loop.scopeDepth);
    loop.breakJumps.push(this.emitJump(Op.Jump));
  }

  continueStatement() {
    if (this.state.loops.len() == 0) {
      this.error("Cannot use 'continue' outside a loop.");
      return;
    }
    this.consume(Tok.Semicolon, "Expect ';' after 'continue'.");
    if (this.breakRunsFinally()) {
      this.exitThroughFinally(F_CONTINUE, false);
      return;
    }
    this.closeLoopHandlers();
    const loop = this.state.loops[this.state.loops.len() - 1];
    this.popLocalsTo(loop.scopeDepth);
    this.emitLoop(loop.continueTarget);
  }

  returnStatement() {
    if (this.state.kind == K_SCRIPT) {
      this.error("Cannot return from top level code.");
    }

    if (this.match(Tok.Semicolon)) {
      if (this.state.finallys.len() == 0) {
        this.emitReturn(true);
        return;
      }
      // Leaving through a finally carries the value on the stack, so the
      // implicit one has to be made explicit.
      if (this.state.kind == K_INITIALIZER) {
        this.emitOp(Op.GetLocal);
        this.emitByte(0);
      } else {
        this.emitOp(Op.Nil);
        this.emitTypeCheck(this.state.returnType);
      }
      this.exitThroughFinally(F_RETURN, true);
      return;
    }

    if (this.state.kind == K_INITIALIZER) {
      this.error("Cannot return a value from an initializer.");
    }
    this.expression();
    this.emitTypeCheck(this.state.returnType);
    this.consume(Tok.Semicolon, "Expect ';' after a return value.");
    if (this.state.finallys.len() == 0) {
      this.emitOp(Op.Return);
      return;
    }
    this.exitThroughFinally(F_RETURN, true);
  }

  // try { A } catch (e: F) { B } ... finally { C }
  //
  // Every way out of A and B goes through C: falling off the end, a
  // caught error, an error nothing matched, and return, break or
  // continue. That is arranged with two hidden slots. The action slot
  // records why control is leaving, the pending slot carries the error or
  // the return value, and one copy of C runs before a dispatch acts on
  // the action.
  //
  // The slots are allocated for every try, not only those with a finally,
  // because in a single pass the finally is not seen until after the body
  // has been compiled.
  tryStatement() {
    this.beginScope();
    this.emitOp(Op.Nil);
    const pendingSlot = this.addHiddenLocal("  pending");
    this.emitConstant(numberConstant(F_FALL_THROUGH));
    const actionSlot = this.addHiddenLocal("  action");

    const context = FinallyContext(actionSlot, pendingSlot,
      this.state.scopeDepth, this.state.tryDepth,
      this.state.loops.len());
    this.state.finallys.push(context);

    const handlerJump = this.emitJump(Op.TryBegin);
    this.state.tryDepth += 1;
    this.beginScope();
    this.consume(Tok.LeftBrace, "Expect '{' after 'try'.");
    this.block();
    this.endScope();
    this.state.tryDepth -= 1;
    this.emitOp(Op.TryEnd);
    const normalJump = this.emitJump(Op.Jump);

    // The error arrives on the stack with everything above the try cut
    // away. It goes into a slot so that every clause can look at it.
    this.patchJump(handlerJump);
    this.beginScope();
    const errorSlot = this.addHiddenLocal("  error");

    // Until a clause claims it, the error is on its way out.
    this.emitOp(Op.GetLocal);
    this.emitByte(errorSlot);
    this.emitOp(Op.SetLocal);
    this.emitByte(pendingSlot);
    this.emitOp(Op.Pop);
    this.emitConstant(numberConstant(F_RETHROW));
    this.emitOp(Op.SetLocal);
    this.emitByte(actionSlot);
    this.emitOp(Op.Pop);

    // The clause bodies get a handler of their own, so that a finally
    // still runs when a catch block is the thing that fails.
    const clauseFailJump = this.emitJump(Op.TryBegin);
    this.state.tryDepth += 1;

    const clauseDone = [];
    let sawCatchAll = false;
    let sawClause = false;

    while (this.check(Tok.Catch)) {
      if (sawCatchAll) {
        this.error("A catch clause cannot follow the one with no filter.");
      }
      this.advance();
      sawClause = true;

      this.consume(Tok.LeftParen, "Expect '(' after 'catch'.");
      this.consume(Tok.Identifier, "Expect an error variable name.");
      const name = this.previous.lexeme;

      let skipJump = -1;
      if (this.match(Tok.Colon)) {
        // A filter is an ordinary expression, so it can be a string kind,
        // a class, or anything that produces one.
        this.emitOp(Op.GetLocal);
        this.emitByte(errorSlot);
        this.expression();
        this.emitOp(Op.CatchMatches);
        skipJump = this.emitJump(Op.JumpIfFalse);
        this.emitOp(Op.Pop);
      } else {
        sawCatchAll = true;
      }
      this.consume(Tok.RightParen, "Expect ')' after the error variable.");
      this.consume(Tok.LeftBrace, "Expect '{' before a catch block.");

      // This clause has claimed the error, so it is no longer on its way
      // out unless the body says otherwise.
      this.emitConstant(numberConstant(F_FALL_THROUGH));
      this.emitOp(Op.SetLocal);
      this.emitByte(actionSlot);
      this.emitOp(Op.Pop);

      this.beginScope();
      this.emitOp(Op.GetLocal);
      this.emitByte(errorSlot);
      this.addLocal(name, false);
      this.markInitialized();
      this.block();
      this.endScope();
      clauseDone.push(this.emitJump(Op.Jump));

      if (skipJump != -1) {
        this.patchJump(skipJump);
        this.emitOp(Op.Pop);
      }
    }

    for (let jump in clauseDone) { this.patchJump(jump); }
    this.state.tryDepth -= 1;
    this.emitOp(Op.TryEnd);
    const handlerDoneJump = this.emitJump(Op.Jump);

    // A catch block threw. Carry its error instead of the original one.
    this.patchJump(clauseFailJump);
    this.emitOp(Op.SetLocal);
    this.emitByte(pendingSlot);
    this.emitOp(Op.Pop);
    this.emitConstant(numberConstant(F_RETHROW));
    this.emitOp(Op.SetLocal);
    this.emitByte(actionSlot);
    this.emitOp(Op.Pop);

    this.patchJump(handlerDoneJump);
    this.endScope();
    const handlerToFinally = this.emitJump(Op.Jump);

    // Everything converges here: the ordinary path, the handler, and any
    // return, break or continue from inside the body.
    this.patchJump(normalJump);
    this.patchJump(handlerToFinally);
    for (let jump in this.state.finallys[this.state.finallys.len() - 1].jumpsToFinally) {
      this.patchJump(jump);
    }
    this.state.finallys.pop();

    let sawFinally = false;
    if (this.match(Tok.Finally)) {
      sawFinally = true;
      this.beginScope();
      this.consume(Tok.LeftBrace, "Expect '{' after 'finally'.");
      this.block();
      this.endScope();
    }

    if (!sawClause and !sawFinally) {
      this.errorAtCurrent("Expect 'catch' or 'finally' after a try block.");
    }

    this.emitFinallyDispatch(context);
    this.endScope();
  }

  throwStatement() {
    this.expression();
    this.consume(Tok.Semicolon, "Expect ';' after a thrown value.");
    this.emitOp(Op.Throw);
  }

  // ---- expressions ----

  expression() { this.parsePrecedence(P_ASSIGNMENT); }

  parsePrecedence(precedence) {
    // Each level of nesting can leave a couple of values pending on the
    // stack, so the deepest nesting bounds what an expression costs. The
    // state is captured here because a lambda pushes a new one part way
    // through, and the count belongs to the function that opened it.
    const owner = this.state;
    owner.nestDepth += 1;
    this.noteTemps(owner.nestDepth * 2);

    this.advance();
    const prefixRule = this.ruleOf(this.previous.type)[0];
    if (prefixRule == nil) {
      this.error("Expect an expression.");
      owner.nestDepth -= 1;
      return;
    }

    const canAssign = precedence <= P_ASSIGNMENT;
    prefixRule(canAssign);

    for (;;) {
      // While parsing the callee of `spawn`, stop before the argument
      // list so that the spawn form can consume it itself.
      if (this.suppressCall and this.check(Tok.LeftParen)) { break; }
      if (precedence > this.ruleOf(this.current.type)[2]) { break; }
      this.advance();
      const infixRule = this.ruleOf(this.previous.type)[1];
      infixRule(canAssign);
    }

    if (canAssign and this.match(Tok.Equal)) {
      this.error("Invalid assignment target.");
    }
    owner.nestDepth -= 1;
  }

  argumentList() {
    const saved = this.allowCalls();
    let count = 0;
    if (!this.check(Tok.RightParen)) {
      for (;;) {
        this.expression();
        if (count == 255) { this.error("Cannot pass more than 255 arguments."); }
        count += 1;
        if (!this.match(Tok.Comma)) { break; }
      }
    }
    this.consume(Tok.RightParen, "Expect ')' after arguments.");
    this.noteTemps(count + 2);
    this.restoreCalls(saved);
    return count;
  }

  grouping(canAssign) {
    const saved = this.allowCalls();
    this.expression();
    this.consume(Tok.RightParen, "Expect ')' after an expression.");
    this.restoreCalls(saved);
  }

  number(canAssign) { this.emitConstant(numberConstant(this.previous.number)); }

  stringLiteral(canAssign) {
    this.emitConstant(stringConstant(this.previous.text));
  }

  interpolation(canAssign) {
    const saved = this.allowCalls();
    // "a${x}b" compiles to the same code as "a" + str(x) + "b". The
    // literal parts are emitted even when empty so the result is always a
    // string.
    this.emitConstant(stringConstant(this.previous.text));
    for (;;) {
      this.expression();
      this.emitOp(Op.ToString);
      this.emitOp(Op.Add);

      if (this.match(Tok.StringInterp)) {
        this.emitConstant(stringConstant(this.previous.text));
        this.emitOp(Op.Add);
        continue;
      }
      if (this.match(Tok.String)) {
        this.emitConstant(stringConstant(this.previous.text));
        this.emitOp(Op.Add);
        break;
      }
      this.errorAtCurrent("Unterminated string interpolation.");
      break;
    }
    this.restoreCalls(saved);
  }

  literal(canAssign) {
    switch (this.previous.type) {
      case Tok.False: {
        this.emitOp(Op.False);
        this.lastPushConstant = boolConstant(false);
      }
      case Tok.Nil: {
        this.emitOp(Op.Nil);
        this.lastPushConstant = nilConstant();
      }
      case Tok.True: {
        this.emitOp(Op.True);
        this.lastPushConstant = boolConstant(true);
      }
      default: return;
    }
    this.lastPush = this.chunk().code.len();
  }

  variable(canAssign) { this.namedVariable(this.previous.lexeme, canAssign); }

  unary(canAssign) {
    const op = this.previous.type;
    this.parsePrecedence(P_UNARY);
    switch (op) {
      case Tok.Bang: this.emitOp(Op.Not);
      case Tok.Minus: this.emitOp(Op.Negate);
      case Tok.Tilde: this.emitOp(Op.BitNot);
    }
  }

  binary(canAssign) {
    const op = this.previous.type;
    // Where the left operand's constant sits, if it was a bare literal.
    const leftConstant = this.lastConstant;
    const rule = this.ruleOf(op);
    this.parsePrecedence(rule[2] + 1);

    // The right operand must have emitted exactly one constant, directly
    // after the left one, for this to be two literals and nothing else.
    if (leftConstant >= 0 and this.lastConstant == leftConstant + 3) {
      if (this.tryFoldBinary(op, leftConstant, this.lastConstant)) { return; }
    }

    switch (op) {
      case Tok.BangEqual: this.emitOp(Op.NotEqual);
      case Tok.EqualEqual: this.emitOp(Op.Equal);
      case Tok.Greater: this.emitOp(Op.Greater);
      case Tok.GreaterEqual: this.emitOp(Op.GreaterEqual);
      case Tok.Less: this.emitOp(Op.Less);
      case Tok.LessEqual: this.emitOp(Op.LessEqual);
      case Tok.Plus: this.emitOp(Op.Add);
      case Tok.Minus: this.emitOp(Op.Subtract);
      case Tok.Star: this.emitOp(Op.Multiply);
      case Tok.Slash: this.emitOp(Op.Divide);
      case Tok.Percent: this.emitOp(Op.Modulo);
      case Tok.Ampersand: this.emitOp(Op.BitAnd);
      case Tok.Pipe: this.emitOp(Op.BitOr);
      case Tok.Caret: this.emitOp(Op.BitXor);
      case Tok.LessLess: this.emitOp(Op.ShiftLeft);
      case Tok.GreaterGreater: this.emitOp(Op.ShiftRight);
    }
  }

  call(canAssign) {
    const argCount = this.argumentList();
    this.emitOp(Op.Call);
    this.emitByte(argCount);
  }

  dot(canAssign) {
    this.consume(Tok.Identifier, "Expect a property name after '.'.");
    const name = this.identifierConstant(this.previous.lexeme);

    if (canAssign and this.match(Tok.Equal)) {
      this.expression();
      this.emitOp(Op.SetProperty);
      this.emitShort(name);
      return;
    }
    const compound = this.matchCompound2(canAssign);
    if (compound != nil) {
      // The receiver is needed twice, once to read the property and once
      // to write it back, so it is duplicated rather than evaluated
      // twice.
      this.emitOp(Op.Dup);
      this.emitOp(Op.GetProperty);
      this.emitShort(name);
      this.expression();
      this.emitOp(compound);
      this.emitOp(Op.SetProperty);
      this.emitShort(name);
    } else if (!this.suppressCall and this.match(Tok.LeftParen)) {
      // Fusing the lookup and the call saves allocating a bound method
      // for the common `obj.method(...)` shape.
      const argCount = this.argumentList();
      this.emitOp(Op.Invoke);
      this.emitShort(name);
      this.emitByte(argCount);
    } else {
      this.emitOp(Op.GetProperty);
      this.emitShort(name);
    }
  }

  index(canAssign) {
    const saved = this.allowCalls();
    this.expression();
    this.consume(Tok.RightBracket, "Expect ']' after an index.");
    if (canAssign and this.match(Tok.Equal)) {
      this.expression();
      this.emitOp(Op.SetIndex);
      this.restoreCalls(saved);
      return;
    }
    const compound = this.matchCompound2(canAssign);
    if (compound != nil) {
      // Both the target and the index are needed twice.
      this.emitOp(Op.Dup2);
      this.emitOp(Op.GetIndex);
      this.expression();
      this.emitOp(compound);
      this.emitOp(Op.SetIndex);
    } else {
      this.emitOp(Op.GetIndex);
    }
    this.restoreCalls(saved);
  }

  andOp(canAssign) {
    const endJump = this.emitJump(Op.JumpIfFalse);
    this.emitOp(Op.Pop);
    this.parsePrecedence(P_AND);
    this.patchJump(endJump);
  }

  orOp(canAssign) {
    const endJump = this.emitJump(Op.JumpIfTrue);
    this.emitOp(Op.Pop);
    this.parsePrecedence(P_OR);
    this.patchJump(endJump);
  }

  arrayLiteral(canAssign) {
    const saved = this.allowCalls();
    let count = 0;
    if (!this.check(Tok.RightBracket)) {
      for (;;) {
        if (this.check(Tok.RightBracket)) { break; } // allow a trailing comma
        this.expression();
        count += 1;
        if (count > 65535) { this.error("Too many elements in an array literal."); }
        if (!this.match(Tok.Comma)) { break; }
      }
    }
    this.consume(Tok.RightBracket, "Expect ']' after array elements.");
    this.noteTemps(count + 2);
    this.emitOp(Op.Array);
    this.emitShort(count);
    this.restoreCalls(saved);
  }

  mapLiteral(canAssign) {
    const saved = this.allowCalls();
    let count = 0;
    if (!this.check(Tok.RightBrace)) {
      for (;;) {
        if (this.check(Tok.RightBrace)) { break; } // allow a trailing comma
        this.expression();
        this.consume(Tok.Colon, "Expect ':' after a map key.");
        this.expression();
        count += 1;
        if (count > 65535) { this.error("Too many entries in a map literal."); }
        if (!this.match(Tok.Comma)) { break; }
      }
    }
    this.consume(Tok.RightBrace, "Expect '}' after map entries.");
    this.noteTemps(count * 2 + 2);
    this.emitOp(Op.Map);
    this.emitShort(count);
    this.restoreCalls(saved);
  }

  lambda(canAssign) { this.function(K_FUNCTION, ""); }

  thisExpr(canAssign) {
    if (this.classState == nil) {
      this.error("Cannot use 'this' outside a class.");
      return;
    }
    this.variable(false);
  }

  superExpr(canAssign) {
    if (this.classState == nil) {
      this.error("Cannot use 'super' outside a class.");
    } else if (!this.classState.hasSuperclass) {
      this.error("Cannot use 'super' in a class with no superclass.");
    }
    this.consume(Tok.Dot, "Expect '.' after 'super'.");
    this.consume(Tok.Identifier, "Expect a superclass method name.");
    const name = this.identifierConstant(this.previous.lexeme);

    this.namedVariable("this", false);
    if (!this.suppressCall and this.match(Tok.LeftParen)) {
      const argCount = this.argumentList();
      this.namedVariable("super", false);
      this.emitOp(Op.SuperInvoke);
      this.emitShort(name);
      this.emitByte(argCount);
    } else {
      this.namedVariable("super", false);
      this.emitOp(Op.GetSuper);
      this.emitShort(name);
    }
  }

  spawnExpr(canAssign) {
    // The callee is parsed with calls suppressed, so `spawn worker(ch)`
    // leaves the argument list here rather than compiling a normal call.
    const previousSuppress = this.suppressCall;
    this.suppressCall = true;
    this.parsePrecedence(P_CALL);
    this.suppressCall = previousSuppress;

    this.consume(Tok.LeftParen, "Expect '(' after a spawn target.");
    const argCount = this.argumentList();
    this.emitOp(Op.Spawn);
    this.emitByte(argCount);
  }

  // ---- entry point ----

  compileScript() {
    const state = FunctionState(nil, K_SCRIPT, Proto());
    state.locals.push(Local("", 0, false, true));
    this.state = state;

    this.advance();
    while (!this.match(Tok.Eof)) { this.declaration(); }
    this.emitReturn(false);
    state.proto.slotCount = state.maxLocals + state.maxTemps + 8;

    this.state = nil;
    if (this.hadError) { return nil; }
    return state.proto;
  }
}

// Compiles source text. Gives back a Proto, or nil when something was
// wrong, in which case the reasons have already been reported.
fun compile(source, modulePath, quiet) {
  const compiler = Compiler(source, modulePath, quiet);
  return compiler.compileScript();
}

// ---------------------------------------------------------------------
// Command line

const EXIT_USAGE = 64;
const EXIT_COMPILE_ERROR = 65;

fun usage() {
  print("The Red compiler, written in Red.");
  print("");
  print("Usage:");
  print("  red selfhost/redc.red compile <in.red> [-o out.redc]");
  print("  red selfhost/redc.red version");
  print("");
  print("Writes the same .redc files as `red compile`. See");
  print("docs/bootstrapping.md for how the two are checked against each");
  print("other.");
}

// Good enough for the path that appears in error messages. The C++
// compiler resolves symlinks as well; nothing depends on that.
fun absoluteOf(path) {
  if (path.starts_with("/")) { return path; }
  return cwd() + "/" + path;
}

// Replaces a .red suffix with .redc, or adds one.
fun compiledNameFor(path) {
  if (path.len() > 4 and path.ends_with(".red")) { return path + "c"; }
  return path + ".redc";
}

fun compileToFile(inPath, outPath) {
  const source = read_file(inPath);
  if (source == nil) {
    reportLine("Cannot open '${inPath}'.");
    return EXIT_USAGE;
  }
  if (source.starts_with(COMPILED_MAGIC)) {
    reportLine("'${inPath}' is already compiled.");
    return EXIT_USAGE;
  }

  const root = compile(source, absoluteOf(inPath), false);
  if (root == nil) { return EXIT_COMPILE_ERROR; }

  const bytes = writeCompiled(root);
  if (write_file(outPath, bytes) != true) {
    reportLine("Cannot write '${outPath}'.");
    return EXIT_USAGE;
  }
  print("${inPath} -> ${outPath} (${bytes.len()} bytes)");
  return 0;
}

fun main() {
  const argv = args();
  if (argv.len() == 0) {
    usage();
    return EXIT_USAGE;
  }

  const command = argv[0];
  if (command == "version") {
    print("redc, written in Red. Bytecode version ${BYTECODE_VERSION}.");
    return 0;
  }
  if (command == "help" or command == "--help" or command == "-h") {
    usage();
    return 0;
  }
  if (command == "compile") {
    if (argv.len() < 2) {
      reportLine("Usage: redc compile <script.red> [-o out.redc]");
      return EXIT_USAGE;
    }
    let outPath = compiledNameFor(argv[1]);
    for (let i in range(2, argv.len() - 1)) {
      if (argv[i] == "-o") { outPath = argv[i + 1]; }
    }
    return compileToFile(argv[1], outPath);
  }

  reportLine("Unknown command '${command}'.");
  usage();
  return EXIT_USAGE;
}

const status = main();
if (status != 0) { exit(status); }
