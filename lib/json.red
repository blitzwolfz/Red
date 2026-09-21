// JSON, read and written.
//
//   import "json.red" as json;
//
//   const config = json.parse(read_file("config.json"));
//   print(config["name"]);
//   print(json.stringify(config));
//   print(json.stringify(config, 2));      // indented
//
// The mapping is the obvious one:
//
//   null   nil          object   map
//   true   true         array    array
//   number number       string   string
//
// Bad input raises an error with kind "json", carrying the position it
// gave up at, so a program can report where the trouble was rather than
// only that there was some.
//
// This is written in Red rather than built into the interpreter on
// purpose: it is the worked example in docs/libraries.md of a library
// that needs nothing native.

// ---------------------------------------------------------------------
// Reading

class Reader {
  init(text) {
    this.text = text;
    this.at = 0;
    this.length = text.len();
  }

  fail(message) {
    throw error("${message} at position ${this.at}", this.at, "json");
  }

  atEnd() { return this.at >= this.length; }

  peek() {
    if (this.at >= this.length) { return ""; }
    return this.text[this.at];
  }

  skipSpace() {
    while (this.at < this.length) {
      const c = this.text[this.at];
      if (c == " " or c == "\t" or c == "\n" or c == "\r") {
        this.at += 1;
      } else {
        return;
      }
    }
  }

  // The next value, whatever it turns out to be.
  value() {
    this.skipSpace();
    if (this.atEnd()) { this.fail("unexpected end of input"); }

    const c = this.peek();
    if (c == "{") { return this.object(); }
    if (c == "[") { return this.array(); }
    if (c == "\"") { return this.string(); }
    if (c == "t") { return this.literal("true", true); }
    if (c == "f") { return this.literal("false", false); }
    if (c == "n") { return this.literal("null", nil); }
    if (c == "-" or (c >= "0" and c <= "9")) { return this.number(); }
    this.fail("unexpected '${c}'");
  }

  literal(word, result) {
    if (this.text.sub(this.at, this.at + word.len()) != word) {
      this.fail("unexpected '${this.peek()}'");
    }
    this.at += word.len();
    return result;
  }

  number() {
    const start = this.at;
    if (this.peek() == "-") { this.at += 1; }
    while (!this.atEnd() and this.peek() >= "0" and this.peek() <= "9") {
      this.at += 1;
    }
    if (this.peek() == ".") {
      this.at += 1;
      while (!this.atEnd() and this.peek() >= "0" and this.peek() <= "9") {
        this.at += 1;
      }
    }
    const e = this.peek();
    if (e == "e" or e == "E") {
      this.at += 1;
      const sign = this.peek();
      if (sign == "+" or sign == "-") { this.at += 1; }
      while (!this.atEnd() and this.peek() >= "0" and this.peek() <= "9") {
        this.at += 1;
      }
    }
    const digits = this.text.sub(start, this.at);
    const parsed = num(digits);
    if (parsed == nil) { this.fail("'${digits}' is not a number"); }
    return parsed;
  }

  string() {
    this.at += 1;                       // the opening quote
    const parts = [];
    for (;;) {
      if (this.atEnd()) { this.fail("unterminated string"); }
      const c = this.text[this.at];
      this.at += 1;

      if (c == "\"") { return parts.join(""); }
      if (c != "\\") {
        parts.push(c);
        continue;
      }

      if (this.atEnd()) { this.fail("unterminated escape"); }
      const escape = this.text[this.at];
      this.at += 1;
      switch (escape) {
        case "\"": parts.push("\"");
        case "\\": parts.push("\\");
        case "/": parts.push("/");
        case "b": parts.push(chr(8));
        case "f": parts.push(chr(12));
        case "n": parts.push("\n");
        case "r": parts.push("\r");
        case "t": parts.push("\t");
        case "u": parts.push(this.escapedCodePoint());
        default: this.fail("unknown escape '\\${escape}'");
      }
    }
  }

  // \uXXXX. JSON writes anything outside the basic plane as a pair of
  // halves, the way UTF-16 does, so a leading half is joined with the
  // one that follows it.
  escapedCodePoint() {
    let code = this.hexQuad();
    if (code >= 55296 and code <= 56319) {
      if (this.text.sub(this.at, this.at + 2) != "\\u") {
        this.fail("a leading surrogate needs a trailing one");
      }
      this.at += 2;
      const low = this.hexQuad();
      if (low < 56320 or low > 57343) {
        this.fail("a leading surrogate needs a trailing one");
      }
      code = 65536 + (code - 55296) * 1024 + (low - 56320);
    } else if (code >= 56320 and code <= 57343) {
      this.fail("a trailing surrogate with nothing in front of it");
    }
    return char(code);
  }

  hexQuad() {
    if (this.at + 4 > this.length) { this.fail("a '\\u' escape needs four hex digits"); }
    let value = 0;
    for (let i in range(0, 4)) {
      const c = this.text[this.at];
      this.at += 1;
      const digit = this.text.sub(this.at - 1, this.at);
      let d = -1;
      if (c >= "0" and c <= "9") { d = c.code_at(0) - 48; }
      else if (c >= "a" and c <= "f") { d = c.code_at(0) - 87; }
      else if (c >= "A" and c <= "F") { d = c.code_at(0) - 55; }
      if (d < 0) { this.fail("'${digit}' is not a hex digit"); }
      value = value * 16 + d;
    }
    return value;
  }

  array() {
    this.at += 1;                       // the opening bracket
    const items = [];
    this.skipSpace();
    if (this.peek() == "]") {
      this.at += 1;
      return items;
    }
    for (;;) {
      items.push(this.value());
      this.skipSpace();
      const c = this.peek();
      if (c == ",") {
        this.at += 1;
        continue;
      }
      if (c == "]") {
        this.at += 1;
        return items;
      }
      this.fail("expected ',' or ']'");
    }
  }

  object() {
    this.at += 1;                       // the opening brace
    const entries = {};
    this.skipSpace();
    if (this.peek() == "}") {
      this.at += 1;
      return entries;
    }
    for (;;) {
      this.skipSpace();
      if (this.peek() != "\"") { this.fail("expected a key in quotes"); }
      const key = this.string();
      this.skipSpace();
      if (this.peek() != ":") { this.fail("expected ':' after a key"); }
      this.at += 1;
      entries.set(key, this.value());

      this.skipSpace();
      const c = this.peek();
      if (c == ",") {
        this.at += 1;
        continue;
      }
      if (c == "}") {
        this.at += 1;
        return entries;
      }
      this.fail("expected ',' or '}'");
    }
  }
}

// Reads one JSON value from `text`. Anything after it, other than
// whitespace, is an error: a file with two values in it is not one
// document and silently keeping the first would hide that.
fun parse(text) {
  const reader = Reader(text);
  const result = reader.value();
  reader.skipSpace();
  if (!reader.atEnd()) { reader.fail("unexpected '${reader.peek()}'"); }
  return result;
}

// ---------------------------------------------------------------------
// Writing

// How deep a structure may be before it is assumed to hold itself. A map
// that contains itself would otherwise be written out for ever.
const MAX_DEPTH = 200;

fun quote(text) {
  const parts = ["\""];
  for (let c in text) {
    const code = c.code_at(0);
    if (c == "\"") { parts.push("\\\""); }
    else if (c == "\\") { parts.push("\\\\"); }
    else if (c == "\n") { parts.push("\\n"); }
    else if (c == "\r") { parts.push("\\r"); }
    else if (c == "\t") { parts.push("\\t"); }
    else if (code == 8) { parts.push("\\b"); }
    else if (code == 12) { parts.push("\\f"); }
    else if (code < 32) {
      // The control characters JSON has no short name for.
      const digits = "0123456789abcdef";
      parts.push("\\u00" + digits[floor(code / 16)] + digits[code % 16]);
    } else {
      // Everything else goes out as the UTF-8 it already is.
      parts.push(c);
    }
  }
  parts.push("\"");
  return parts.join("");
}

fun writeNumber(value) {
  if (value != value) { throw error("JSON has no way to write nan", value, "json"); }
  if (value > 1.7976931348623157e308 or value < -1.7976931348623157e308) {
    throw error("JSON has no way to write ${value}", value, "json");
  }
  return str(value);
}

fun writeValue(value, indent, depth, parts) {
  if (depth > MAX_DEPTH) {
    throw error("JSON structure is deeper than ${MAX_DEPTH}, or holds itself",
                nil, "json");
  }

  const kind = type(value);
  if (value == nil) { parts.push("null"); return; }
  if (kind == "bool") { parts.push(str(value)); return; }
  if (kind == "number") { parts.push(writeNumber(value)); return; }
  if (kind == "string") { parts.push(quote(value)); return; }

  // The separators, which are all that changes between the compact form
  // and the indented one.
  let open = "";
  let between = ",";
  let close = "";
  if (indent > 0) {
    const inner = " ".repeat(indent * (depth + 1));
    const outer = " ".repeat(indent * depth);
    open = "\n" + inner;
    between = ",\n" + inner;
    close = "\n" + outer;
  }

  if (kind == "array") {
    if (value.len() == 0) { parts.push("[]"); return; }
    parts.push("[" + open);
    for (let i in range(0, value.len())) {
      if (i > 0) { parts.push(between); }
      writeValue(value[i], indent, depth + 1, parts);
    }
    parts.push(close + "]");
    return;
  }

  if (kind == "map") {
    const keys = value.keys();
    keys.sort();
    if (keys.len() == 0) { parts.push("{}"); return; }
    parts.push("{" + open);
    let first = true;
    for (let key in keys) {
      if (!first) { parts.push(between); }
      first = false;
      const keyKind = type(key);
      if (keyKind == "string") {
        parts.push(quote(key));
      } else if (keyKind == "number") {
        parts.push(quote(str(key)));
      } else {
        throw error("a JSON key must be a string or a number, not ${keyKind}",
                    key, "json");
      }
      parts.push(":");
      if (indent > 0) { parts.push(" "); }
      writeValue(value[key], indent, depth + 1, parts);
    }
    parts.push(close + "}");
    return;
  }

  throw error("JSON has no way to write a ${kind}", value, "json");
}

// stringify(value) gives the compact form on one line.
// stringify(value, indent) indents nested values by that many spaces.
//
// Map keys come out sorted, so the same data always produces the same
// text and two outputs can be compared.
fun stringify(value, indent = 0) {
  const parts = [];
  writeValue(value, indent, 0, parts);
  return parts.join("");
}
