// A small compiler and virtual machine, written in Red.
//
//   red examples/mini_compiler.red
//
// This is a rehearsal for the self-hosted compiler described in
// docs/bootstrapping.md. It is deliberately tiny, an arithmetic language
// and nothing else, but it uses every part of Red that the real port
// depends on:
//
//   enums used as array indexes and switch cases
//   a Pratt rule table holding bound methods as values
//   bitwise arithmetic to pack two byte operands
//   chr() to turn the finished code into bytes
//   destructuring to read structured results back
//
// If this runs, nothing in the language is in the way of the real thing.

enum Tok { Number, Plus, Minus, Star, Slash, LeftParen, RightParen, End }

enum Op { Constant, Add, Subtract, Multiply, Divide, Negate, Return }

// Binding power. Higher numbers bind more tightly.
enum Prec { None, Term, Factor, Unary }

// ---- scanner --------------------------------------------------------

class Token {
  init(kind, number) {
    this.kind = kind;
    this.number = number;
  }
}

class Scanner {
  init(source) {
    this.source = source;
    this.at = 0;
  }

  atEnd() { return this.at >= this.source.len(); }

  peek() {
    if (this.atEnd()) { return ""; }
    return this.source[this.at];
  }

  scan() {
    while (!this.atEnd() and this.peek() == " ") { this.at += 1; }
    if (this.atEnd()) { return Token(Tok.End, 0); }

    const c = this.peek();
    if (c >= "0" and c <= "9") {
      let digits = "";
      while (!this.atEnd() and this.peek() >= "0" and this.peek() <= "9") {
        digits += this.peek();
        this.at += 1;
      }
      return Token(Tok.Number, num(digits));
    }

    this.at += 1;
    switch (c) {
      case "+": return Token(Tok.Plus, 0);
      case "-": return Token(Tok.Minus, 0);
      case "*": return Token(Tok.Star, 0);
      case "/": return Token(Tok.Slash, 0);
      case "(": return Token(Tok.LeftParen, 0);
      case ")": return Token(Tok.RightParen, 0);
      default: throw error("unexpected character '${c}'", nil, "syntax");
    }
  }
}

// ---- compiler -------------------------------------------------------

class Compiler {
  init(source) {
    this.scanner = Scanner(source);
    this.current = this.scanner.scan();
    this.previous = nil;
    this.code = [];
    this.constants = [];

    // One rule per token kind, looked up by the member's value. Each
    // rule holds bound methods, which keep their receiver.
    this.rules = [];
    for (let i in range(0, len(Tok))) {
      this.rules.push([nil, nil, Prec.None]);
    }
    this.rules[Tok.Number.value] = [this.number, nil, Prec.None];
    this.rules[Tok.LeftParen.value] = [this.grouping, nil, Prec.None];
    this.rules[Tok.Minus.value] = [this.unary, this.binary, Prec.Term];
    this.rules[Tok.Plus.value] = [nil, this.binary, Prec.Term];
    this.rules[Tok.Star.value] = [nil, this.binary, Prec.Factor];
    this.rules[Tok.Slash.value] = [nil, this.binary, Prec.Factor];
  }

  advance() {
    this.previous = this.current;
    this.current = this.scanner.scan();
  }

  expect(kind, what) {
    if (this.current.kind != kind) {
      throw error("expected ${what}", nil, "syntax");
    }
    this.advance();
  }

  emit(byte) { this.code.push(byte & 255); }

  // A two byte operand, split exactly the way the C++ emitter does it.
  emitShort(value) {
    this.emit(value >> 8 & 255);
    this.emit(value & 255);
  }

  emitConstant(value) {
    let index = this.constants.index_of(value);
    if (index < 0) {
      this.constants.push(value);
      index = this.constants.len() - 1;
    }
    this.emit(Op.Constant.value);
    this.emitShort(index);
  }

  ruleFor(kind) { return this.rules[kind.value]; }

  parse(precedence) {
    this.advance();
    const [prefix, ignored, alsoIgnored] = this.ruleFor(this.previous.kind);
    if (prefix == nil) {
      throw error("expected an expression", nil, "syntax");
    }
    prefix();

    for (;;) {
      const [unused, infix, infixPrecedence] = this.ruleFor(this.current.kind);
      if (precedence.value > infixPrecedence.value) { break; }
      this.advance();
      infix();
    }
  }

  number() { this.emitConstant(this.previous.number); }

  grouping() {
    this.parse(Prec.Term);
    this.expect(Tok.RightParen, "')'");
  }

  unary() {
    this.parse(Prec.Unary);
    this.emit(Op.Negate.value);
  }

  binary() {
    const kind = this.previous.kind;
    const [a, b, precedence] = this.ruleFor(kind);
    // Left associative, so the right side binds one level tighter.
    this.parse(Prec.from(precedence.value + 1));
    switch (kind) {
      case Tok.Plus: this.emit(Op.Add.value);
      case Tok.Minus: this.emit(Op.Subtract.value);
      case Tok.Star: this.emit(Op.Multiply.value);
      default: this.emit(Op.Divide.value);
    }
  }

  compile() {
    this.parse(Prec.Term);
    this.expect(Tok.End, "the end of the expression");
    this.emit(Op.Return.value);
    return [this.code, this.constants];
  }
}

// ---- virtual machine ------------------------------------------------

fun run(code, constants) {
  let stack = [];
  let ip = 0;
  for (;;) {
    const instruction = Op.from(code[ip]);
    ip += 1;
    switch (instruction) {
      case Op.Constant:
        const index = (code[ip] << 8) | code[ip + 1];
        ip += 2;
        stack.push(constants[index]);
      case Op.Add:
        const right = stack.pop();
        stack.push(stack.pop() + right);
      case Op.Subtract:
        const subtrahend = stack.pop();
        stack.push(stack.pop() - subtrahend);
      case Op.Multiply:
        const factor = stack.pop();
        stack.push(stack.pop() * factor);
      case Op.Divide:
        const divisor = stack.pop();
        if (divisor == 0) { throw error("divide by zero", nil, "maths"); }
        stack.push(stack.pop() / divisor);
      case Op.Negate:
        stack.push(-stack.pop());
      default:
        return stack.pop();
    }
  }
}

fun disassemble(code, constants) {
  let lines = [];
  let ip = 0;
  while (ip < code.len()) {
    const instruction = Op.from(code[ip]);
    let line = "${ip}".repeat(1) + "  " + instruction.name;
    if (instruction == Op.Constant) {
      const index = (code[ip + 1] << 8) | code[ip + 2];
      line += " ${index} (${constants[index]})";
      ip += 3;
    } else {
      ip += 1;
    }
    lines.push(line);
  }
  return lines;
}

// ---- driver ---------------------------------------------------------

const programs = [
  "2 + 3 * 4",
  "(2 + 3) * 4",
  "10 - 2 - 3",
  "-(4 + 1) * 2",
  "100 / 5 / 2",
];

for (let source in programs) {
  const [code, constants] = Compiler(source).compile();
  print("${source} = ${run(code, constants)}  (${code.len()} bytes)");
}

print("");
print("bytecode for '(2 + 3) * 4':");
const [code, constants] = Compiler("(2 + 3) * 4").compile();
for (let line in disassemble(code, constants)) { print("  ${line}"); }

// The finished code is bytes, so it can be written out and read back.
// This is what the real compiler will do with a .redc file.
let blob = "";
for (let byte in code) { blob += chr(byte); }
write_file("./mini_output.tmp", blob);
const reloaded = read_file("./mini_output.tmp").bytes();
print("");
// equals() compares contents. == on two arrays asks whether they are the
// same array, which they are not.
print("wrote ${blob.len()} bytes, read back ${reloaded.len()}, " +
  "same contents: ${reloaded.equals(code)}, same array: ${reloaded == code}");
remove_file("./mini_output.tmp");

// Errors carry kinds, so the driver can separate a bad program from a
// bug in the compiler.
for (let bad in ["2 +", "2 $ 3", "1 / 0"]) {
  try {
    const [badCode, badConstants] = Compiler(bad).compile();
    run(badCode, badConstants);
  } catch (e: "syntax") {
    print("syntax error in '${bad}': ${e.message}");
  } catch (e: "maths") {
    print("runtime error in '${bad}': ${e.message}");
  }
}
