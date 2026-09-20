#include "compiler.h"

#include <cstdarg>
#include <cstdio>
#include <unordered_set>

#include "debug.h"
#include "scanner.h"

namespace red {

namespace {

enum class Precedence {
  None,
  Assignment,  // =
  Or,          // or
  And,         // and
  Equality,    // == !=
  Comparison,  // < > <= >=
  BitOr,       // |
  BitXor,      // ^
  BitAnd,      // &
  Shift,       // << >>
  Term,        // + -
  Factor,      // * / %
  Unary,       // ! -
  Call,        // . () []
  Primary,
};

enum class FunctionKind {
  Script,
  Function,
  Method,
  Initializer,
};

struct Local {
  std::string name;
  int depth;
  bool isCaptured;
  bool isConst;
};

struct CompilerUpvalue {
  uint8_t index;
  bool isLocal;
};

// Tracks where break and continue should jump for the innermost loop.
struct LoopState {
  int continueTarget;
  int scopeDepth;
  // Active try blocks when the loop began. Leaving the loop from inside a
  // try has to drop the handlers opened since.
  int tryDepth;
  std::vector<int> breakJumps;
};

// A destructuring pattern, built while parsing and emitted once the
// subject is on the stack.
struct Pattern;

struct PatternBinding {
  enum class Source { Index, Field, Rest };
  Source source = Source::Index;
  // Position for Index and Rest.
  int index = 0;
  // Field name for Field.
  std::string field;
  // Name to bind, empty when this binding holds a nested pattern.
  std::string name;
  std::shared_ptr<Pattern> nested;
};

struct Pattern {
  bool isArray = true;
  std::vector<PatternBinding> bindings;
};

constexpr int kMaxLocals = 256;
constexpr int kMaxUpvalues = 256;

}  // namespace

class Compiler;
using ParseFn = void (Compiler::*)(bool canAssign);

struct ParseRule {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
};

// One per function being compiled. They form a stack through `enclosing`,
// which is how upvalue capture walks outwards.
struct FunctionState {
  FunctionState* enclosing = nullptr;
  ObjFunction* function = nullptr;
  FunctionKind kind = FunctionKind::Script;
  std::vector<Local> locals;
  CompilerUpvalue upvalues[kMaxUpvalues];
  int scopeDepth = 0;
  std::vector<LoopState> loops;
  // Try blocks currently open in this function.
  int tryDepth = 0;
  // Worst case stack use, used to fill ObjFunction::slotCount.
  int maxLocals = 0;
  int maxTemps = 0;
  int nestDepth = 0;
};

struct ClassState {
  ClassState* enclosing = nullptr;
  bool hasSuperclass = false;
};

class Compiler {
 public:
  Compiler(Runtime& runtime, const std::string& source, ObjModule* module,
           bool quiet)
      : runtime_(runtime), scanner_(source), module_(module), quiet_(quiet) {}

  ObjFunction* compileScript();

 private:
  Runtime& runtime_;
  Scanner scanner_;
  ObjModule* module_;
  Token current_;
  Token previous_;
  bool quiet_ = false;
  bool hadError_ = false;
  bool panicMode_ = false;
  FunctionState* state_ = nullptr;
  ClassState* classState_ = nullptr;
  // Names declared const at module level. Checked at compile time only.
  std::unordered_set<std::string> constGlobals_;
  // Set while parsing the callee of `spawn`, so that the argument list is
  // left for the spawn form itself to consume. It applies only to the
  // callee's own top level, so any nested context clears it with the
  // guard below.
  bool suppressCall_ = false;

  // Clears suppressCall_ for the lifetime of the guard. Every construct
  // that opens a bracket and parses expressions inside it needs one, or a
  // call written inside `spawn f(...)` would be suppressed too.
  class AllowCalls {
   public:
    explicit AllowCalls(Compiler& compiler)
        : compiler_(compiler), saved_(compiler.suppressCall_) {
      compiler_.suppressCall_ = false;
    }
    ~AllowCalls() { compiler_.suppressCall_ = saved_; }

   private:
    Compiler& compiler_;
    bool saved_;
  };

  Chunk& chunk() { return state_->function->chunk; }

  // Raises the recorded worst case number of temporaries.
  void noteTemps(int count) {
    if (count > state_->maxTemps) state_->maxTemps = count;
  }

  // ---- token plumbing ----
  void advance();
  void consume(TokenType type, const char* message);
  bool check(TokenType type) const { return current_.type == type; }
  bool match(TokenType type);
  void errorAt(const Token& token, const std::string& message);
  void error(const std::string& message) { errorAt(previous_, message); }
  void errorAtCurrent(const std::string& message) {
    errorAt(current_, message);
  }
  void synchronize();

  // ---- emitting ----
  void emitByte(uint8_t byte);
  void emitBytes(uint8_t a, uint8_t b);
  void emitShort(int value);
  int emitJump(uint8_t instruction);
  void patchJump(int offset);
  void emitLoop(int loopStart);
  void emitReturn();
  int makeConstant(Value value);
  void emitConstant(Value value);
  int identifierConstant(const std::string& name);

  // ---- scopes and variables ----
  void beginScope() { state_->scopeDepth++; }
  void endScope();
  void addLocal(const std::string& name, bool isConst);
  // Takes the name rather than reading the last token, because patterns
  // declare names long after the token that carried them.
  void declareVariable(const std::string& name, bool isConst);
  // Declares a hidden slot that user code cannot name. Hidden names carry
  // a space so they can never collide with an identifier.
  int addHiddenLocal(const char* name);
  int parseVariable(const char* message, bool isConst);
  // The part of parseVariable that runs once the name has been consumed.
  // for-in needs to read the name first to see whether "in" follows.
  int declareParsedVariable(const std::string& name, bool isConst);
  void markInitialized();
  void defineVariable(int global, bool isConst);
  int resolveLocal(FunctionState* state, const std::string& name);
  int resolveUpvalue(FunctionState* state, const std::string& name);
  int addUpvalue(FunctionState* state, uint8_t index, bool isLocal);
  // Takes the name by value on purpose. It usually comes from
  // previous_.lexeme, and match() below overwrites that token.
  void namedVariable(std::string name, bool canAssign);
  // Consumes an optional `: Type` annotation and returns its text.
  std::string typeAnnotation();

  // ---- declarations and statements ----
  void declaration();
  void classDeclaration();
  void enumDeclaration();
  void funDeclaration();
  void varDeclaration(bool isConst);
  void importDeclaration();
  void statement();
  void expressionStatement();
  void block();
  void ifStatement();
  void whileStatement();
  void forStatement();
  void forInStatement(const std::string& name,
                      const std::shared_ptr<Pattern>& pattern);
  std::shared_ptr<Pattern> parsePattern();
  // Emits the bindings of a pattern, reading from the local at
  // subjectSlot. Returns how many hidden slots it added.
  int emitPattern(const Pattern& pattern, int subjectSlot, bool isConst);
  void destructuringDeclaration(bool isConst);
  void switchStatement();
  void caseBody();
  void returnStatement();
  void breakStatement();
  void continueStatement();
  void tryStatement();
  void throwStatement();
  void popLoopLocals(int targetDepth);
  void closeLoopHandlers();

  void function(FunctionKind kind, const std::string& name);
  void method();

  // ---- expressions ----
  void expression();
  void parsePrecedence(Precedence precedence);
  uint8_t argumentList();
  // Matches a compound assignment operator such as "+=" and reports the
  // arithmetic opcode that goes with it.
  bool matchCompound(uint8_t* op);

  void grouping(bool canAssign);
  void number(bool canAssign);
  void stringLiteral(bool canAssign);
  void interpolation(bool canAssign);
  void literal(bool canAssign);
  void variable(bool canAssign);
  void unary(bool canAssign);
  void binary(bool canAssign);
  void call(bool canAssign);
  void dot(bool canAssign);
  void index(bool canAssign);
  void andOp(bool canAssign);
  void orOp(bool canAssign);
  void arrayLiteral(bool canAssign);
  void mapLiteral(bool canAssign);
  void lambda(bool canAssign);
  void thisExpr(bool canAssign);
  void superExpr(bool canAssign);
  void spawnExpr(bool canAssign);

  static const ParseRule* getRule(TokenType type);
};

// ---------------------------------------------------------------------
// token plumbing

void Compiler::advance() {
  previous_ = current_;
  for (;;) {
    current_ = scanner_.scan();
    if (current_.type != TokenType::Error) break;
    errorAtCurrent(current_.lexeme);
  }
}

void Compiler::consume(TokenType type, const char* message) {
  if (current_.type == type) {
    advance();
    return;
  }
  errorAtCurrent(message);
}

bool Compiler::match(TokenType type) {
  if (!check(type)) return false;
  advance();
  return true;
}

void Compiler::errorAt(const Token& token, const std::string& message) {
  // One report per statement. After the first, everything until the next
  // statement boundary is noise caused by the first.
  if (panicMode_) return;
  panicMode_ = true;
  hadError_ = true;
  if (quiet_) return;

  std::fprintf(stderr, "[%s line %d] Error",
               module_->path->chars, token.line);
  if (token.type == TokenType::Eof) {
    std::fprintf(stderr, " at end");
  } else if (token.type != TokenType::Error) {
    std::fprintf(stderr, " at '%s'", token.lexeme.c_str());
  }
  std::fprintf(stderr, ": %s\n", message.c_str());
}

void Compiler::synchronize() {
  panicMode_ = false;
  while (current_.type != TokenType::Eof) {
    if (previous_.type == TokenType::Semicolon) return;
    switch (current_.type) {
      case TokenType::Class:
      case TokenType::Enum:
      case TokenType::Fun:
      case TokenType::Let:
      case TokenType::Const:
      case TokenType::For:
      case TokenType::If:
      case TokenType::While:
      case TokenType::Switch:
      case TokenType::Return:
      case TokenType::Try:
      case TokenType::Throw:
      case TokenType::Import:
        return;
      default:
        break;
    }
    advance();
  }
}

// ---------------------------------------------------------------------
// emitting

void Compiler::emitByte(uint8_t byte) { chunk().write(byte, previous_.line); }

void Compiler::emitBytes(uint8_t a, uint8_t b) {
  emitByte(a);
  emitByte(b);
}

void Compiler::emitShort(int value) {
  emitByte((uint8_t)((value >> 8) & 0xff));
  emitByte((uint8_t)(value & 0xff));
}

int Compiler::emitJump(uint8_t instruction) {
  emitByte(instruction);
  emitShort(0xffff);
  return (int)chunk().code.size() - 2;
}

void Compiler::patchJump(int offset) {
  int jump = (int)chunk().code.size() - offset - 2;
  if (jump > 0xffff) error("Too much code to jump over.");
  chunk().code[(size_t)offset] = (uint8_t)((jump >> 8) & 0xff);
  chunk().code[(size_t)offset + 1] = (uint8_t)(jump & 0xff);
}

void Compiler::emitLoop(int loopStart) {
  emitByte(OP_LOOP);
  int offset = (int)chunk().code.size() - loopStart + 2;
  if (offset > 0xffff) error("Loop body too large.");
  emitShort(offset);
}

void Compiler::emitReturn() {
  if (state_->kind == FunctionKind::Initializer) {
    // An initializer always answers with the instance, whatever the body
    // did, so `let a = Animal("cat")` never yields nil by accident.
    emitByte(OP_GET_LOCAL);
    emitByte(0);
  } else {
    emitByte(OP_NIL);
  }
  emitByte(OP_RETURN);
}

int Compiler::makeConstant(Value value) {
  int constant = chunk().addConstant(value);
  if (constant > 0xffff) {
    error("Too many constants in one chunk.");
    return 0;
  }
  return constant;
}

void Compiler::emitConstant(Value value) {
  emitByte(OP_CONSTANT);
  emitShort(makeConstant(value));
}

int Compiler::identifierConstant(const std::string& name) {
  return makeConstant(objValue((Obj*)runtime_.internString(name)));
}

// ---------------------------------------------------------------------
// scopes and variables

void Compiler::endScope() {
  state_->scopeDepth--;
  while (!state_->locals.empty() &&
         state_->locals.back().depth > state_->scopeDepth) {
    // A captured local lives on in an upvalue, so it has to be moved to
    // the heap rather than simply discarded.
    if (state_->locals.back().isCaptured) {
      emitByte(OP_CLOSE_UPVALUE);
    } else {
      emitByte(OP_POP);
    }
    state_->locals.pop_back();
  }
}

void Compiler::addLocal(const std::string& name, bool isConst) {
  if ((int)state_->locals.size() >= kMaxLocals) {
    error("Too many local variables in function.");
    return;
  }
  state_->locals.push_back({name, -1, false, isConst});
  if ((int)state_->locals.size() > state_->maxLocals) {
    state_->maxLocals = (int)state_->locals.size();
  }
}

void Compiler::declareVariable(const std::string& name, bool isConst) {
  if (state_->scopeDepth == 0) return;
  for (int i = (int)state_->locals.size() - 1; i >= 0; i--) {
    Local& local = state_->locals[(size_t)i];
    if (local.depth != -1 && local.depth < state_->scopeDepth) break;
    if (local.name == name) {
      error("A variable with this name already exists in this scope.");
    }
  }
  addLocal(name, isConst);
}

int Compiler::parseVariable(const char* message, bool isConst) {
  consume(TokenType::Identifier, message);
  return declareParsedVariable(previous_.lexeme, isConst);
}

int Compiler::declareParsedVariable(const std::string& name, bool isConst) {
  declareVariable(name, isConst);
  if (state_->scopeDepth > 0) return 0;
  if (isConst) constGlobals_.insert(name);
  return identifierConstant(name);
}

int Compiler::addHiddenLocal(const char* name) {
  addLocal(name, true);
  // Usable straight away, including at the top level where
  // markInitialized does nothing.
  state_->locals.back().depth = state_->scopeDepth;
  return (int)state_->locals.size() - 1;
}

void Compiler::markInitialized() {
  if (state_->scopeDepth == 0) return;
  state_->locals.back().depth = state_->scopeDepth;
}

void Compiler::defineVariable(int global, bool isConst) {
  (void)isConst;
  if (state_->scopeDepth > 0) {
    markInitialized();
    return;
  }
  emitByte(OP_DEFINE_GLOBAL);
  emitShort(global);
}

int Compiler::resolveLocal(FunctionState* state, const std::string& name) {
  for (int i = (int)state->locals.size() - 1; i >= 0; i--) {
    if (state->locals[(size_t)i].name == name) {
      if (state->locals[(size_t)i].depth == -1) {
        error("Cannot read a local variable inside its own initializer.");
      }
      return i;
    }
  }
  return -1;
}

int Compiler::addUpvalue(FunctionState* state, uint8_t index, bool isLocal) {
  int count = state->function->upvalueCount;
  for (int i = 0; i < count; i++) {
    if (state->upvalues[i].index == index &&
        state->upvalues[i].isLocal == isLocal) {
      return i;
    }
  }
  if (count == kMaxUpvalues) {
    error("Too many closure variables in function.");
    return 0;
  }
  state->upvalues[count].isLocal = isLocal;
  state->upvalues[count].index = index;
  return state->function->upvalueCount++;
}

int Compiler::resolveUpvalue(FunctionState* state, const std::string& name) {
  if (state->enclosing == nullptr) return -1;

  int local = resolveLocal(state->enclosing, name);
  if (local != -1) {
    state->enclosing->locals[(size_t)local].isCaptured = true;
    return addUpvalue(state, (uint8_t)local, true);
  }
  // Not a direct parent local, so look further out. Each level adds one
  // hop, which is what makes deeply nested closures work.
  int upvalue = resolveUpvalue(state->enclosing, name);
  if (upvalue != -1) return addUpvalue(state, (uint8_t)upvalue, false);
  return -1;
}

void Compiler::namedVariable(std::string name, bool canAssign) {
  uint8_t getOp, setOp;
  bool isConstLocal = false;
  int arg = resolveLocal(state_, name);
  if (arg != -1) {
    getOp = OP_GET_LOCAL;
    setOp = OP_SET_LOCAL;
    isConstLocal = state_->locals[(size_t)arg].isConst;
  } else if ((arg = resolveUpvalue(state_, name)) != -1) {
    getOp = OP_GET_UPVALUE;
    setOp = OP_SET_UPVALUE;
  } else {
    arg = identifierConstant(name);
    getOp = OP_GET_GLOBAL;
    setOp = OP_SET_GLOBAL;
  }

  bool isConstBinding =
      isConstLocal ||
      (getOp == OP_GET_GLOBAL && constGlobals_.count(name) > 0);

  uint8_t compound;
  if (canAssign && matchCompound(&compound)) {
    if (isConstBinding) error("Cannot assign to a const binding.");
    // Read the current value, combine it, and write it back.
    emitByte(getOp);
    if (getOp == OP_GET_GLOBAL) emitShort(arg); else emitByte((uint8_t)arg);
    expression();
    emitByte(compound);
    emitByte(setOp);
    if (setOp == OP_SET_GLOBAL) emitShort(arg); else emitByte((uint8_t)arg);
  } else if (canAssign && match(TokenType::Equal)) {
    if (isConstBinding) error("Cannot assign to a const binding.");
    expression();
    emitByte(setOp);
    if (setOp == OP_SET_GLOBAL) {
      emitShort(arg);
    } else {
      emitByte((uint8_t)arg);
    }
  } else {
    emitByte(getOp);
    if (getOp == OP_GET_GLOBAL) {
      emitShort(arg);
    } else {
      emitByte((uint8_t)arg);
    }
  }
}

std::string Compiler::typeAnnotation() {
  if (!match(TokenType::Colon)) return "";
  consume(TokenType::Identifier, "Expect a type name after ':'.");
  std::string name = previous_.lexeme;
  // Allow array-of and map-of spellings such as `[Int]` without giving
  // them meaning. Nothing checks annotations.
  while (match(TokenType::LeftBracket)) {
    consume(TokenType::RightBracket, "Expect ']' in type name.");
    name += "[]";
  }
  return name;
}

// ---------------------------------------------------------------------
// declarations

void Compiler::declaration() {
  if (match(TokenType::Class)) {
    classDeclaration();
  } else if (match(TokenType::Enum)) {
    enumDeclaration();
  } else if (match(TokenType::Fun)) {
    funDeclaration();
  } else if (match(TokenType::Let)) {
    varDeclaration(false);
  } else if (match(TokenType::Const)) {
    varDeclaration(true);
  } else if (match(TokenType::Import)) {
    importDeclaration();
  } else {
    statement();
  }
  if (panicMode_) synchronize();
}

std::shared_ptr<Pattern> Compiler::parsePattern() {
  auto pattern = std::make_shared<Pattern>();

  if (match(TokenType::LeftBracket)) {
    pattern->isArray = true;
    int index = 0;
    if (!check(TokenType::RightBracket)) {
      do {
        if (check(TokenType::RightBracket)) break;
        PatternBinding binding;
        if (match(TokenType::Ellipsis)) {
          // A rest binding takes everything left, so nothing follows it.
          binding.source = PatternBinding::Source::Rest;
          binding.index = index;
          consume(TokenType::Identifier, "Expect a name after '...'.");
          binding.name = previous_.lexeme;
          pattern->bindings.push_back(binding);
          break;
        }
        binding.source = PatternBinding::Source::Index;
        binding.index = index++;
        if (check(TokenType::LeftBracket) || check(TokenType::LeftBrace)) {
          binding.nested = parsePattern();
        } else {
          consume(TokenType::Identifier, "Expect a name in a pattern.");
          binding.name = previous_.lexeme;
        }
        pattern->bindings.push_back(binding);
      } while (match(TokenType::Comma));
    }
    consume(TokenType::RightBracket, "Expect ']' after a pattern.");
    return pattern;
  }

  consume(TokenType::LeftBrace, "Expect '[' or '{' to start a pattern.");
  pattern->isArray = false;
  if (!check(TokenType::RightBrace)) {
    do {
      if (check(TokenType::RightBrace)) break;
      PatternBinding binding;
      binding.source = PatternBinding::Source::Field;
      consume(TokenType::Identifier, "Expect a field name in a pattern.");
      binding.field = previous_.lexeme;
      binding.name = binding.field;
      if (match(TokenType::Colon)) {
        if (check(TokenType::LeftBracket) || check(TokenType::LeftBrace)) {
          binding.nested = parsePattern();
          binding.name.clear();
        } else {
          consume(TokenType::Identifier, "Expect a name after ':'.");
          binding.name = previous_.lexeme;
        }
      }
      pattern->bindings.push_back(binding);
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightBrace, "Expect '}' after a pattern.");
  return pattern;
}

int Compiler::emitPattern(const Pattern& pattern, int subjectSlot,
                          bool isConst) {
  int hidden = 0;
  for (const PatternBinding& binding : pattern.bindings) {
    emitByte(OP_GET_LOCAL);
    emitByte((uint8_t)subjectSlot);
    switch (binding.source) {
      case PatternBinding::Source::Index:
        emitByte(OP_DESTRUCTURE_INDEX);
        emitShort(binding.index);
        break;
      case PatternBinding::Source::Rest:
        emitByte(OP_DESTRUCTURE_REST);
        emitShort(binding.index);
        break;
      case PatternBinding::Source::Field:
        emitByte(OP_DESTRUCTURE_FIELD);
        emitShort(identifierConstant(binding.field));
        break;
    }

    if (binding.nested != nullptr) {
      // The extracted value becomes the subject of the inner pattern.
      int nestedSlot = addHiddenLocal("  nested");
      hidden++;
      hidden += emitPattern(*binding.nested, nestedSlot, isConst);
      continue;
    }

    int global = declareParsedVariable(binding.name, isConst);
    defineVariable(global, isConst);
  }
  return hidden;
}

void Compiler::destructuringDeclaration(bool isConst) {
  std::shared_ptr<Pattern> pattern = parsePattern();
  consume(TokenType::Equal, "A destructuring declaration needs a value.");
  expression();
  consume(TokenType::Semicolon, "Expect ';' after a variable declaration.");

  int subjectSlot = addHiddenLocal("  subject");
  int hidden = emitPattern(*pattern, subjectSlot, isConst) + 1;

  if (state_->scopeDepth == 0) {
    // At the top level the bindings became globals and popped themselves,
    // so only the hidden slots are left to clear.
    for (int i = 0; i < hidden; i++) {
      emitByte(OP_POP);
      state_->locals.pop_back();
    }
  }
  // Inside a scope the hidden slots are ordinary locals, and the closing
  // brace pops them along with everything else.
}

void Compiler::varDeclaration(bool isConst) {
  if (check(TokenType::LeftBracket) || check(TokenType::LeftBrace)) {
    destructuringDeclaration(isConst);
    return;
  }
  int global = parseVariable("Expect a variable name.", isConst);
  typeAnnotation();

  if (match(TokenType::Equal)) {
    expression();
  } else if (isConst) {
    error("A const binding must have an initializer.");
    emitByte(OP_NIL);
  } else {
    emitByte(OP_NIL);
  }
  consume(TokenType::Semicolon, "Expect ';' after a variable declaration.");
  defineVariable(global, isConst);
}

void Compiler::funDeclaration() {
  int global = parseVariable("Expect a function name.", false);
  std::string name = previous_.lexeme;
  // Marked before the body is compiled so a function can call itself.
  markInitialized();
  function(FunctionKind::Function, name);
  defineVariable(global, false);
}

void Compiler::importDeclaration() {
  consume(TokenType::String, "Expect a module path string after 'import'.");
  std::string path = previous_.text;
  int pathConstant = makeConstant(objValue((Obj*)runtime_.internString(path)));

  std::string binding;
  if (match(TokenType::As)) {
    consume(TokenType::Identifier, "Expect a name after 'as'.");
    binding = previous_.lexeme;
  } else {
    // Default the binding to the file stem, so `import "util.red";` binds
    // the name `util`.
    size_t slash = path.find_last_of('/');
    binding = slash == std::string::npos ? path : path.substr(slash + 1);
    size_t dot = binding.find_last_of('.');
    if (dot != std::string::npos) binding = binding.substr(0, dot);
  }
  consume(TokenType::Semicolon, "Expect ';' after an import.");

  emitByte(OP_IMPORT);
  emitShort(pathConstant);

  if (state_->scopeDepth > 0) {
    addLocal(binding, true);
    markInitialized();
  } else {
    int global = identifierConstant(binding);
    constGlobals_.insert(binding);
    emitByte(OP_DEFINE_GLOBAL);
    emitShort(global);
  }
}

// enum Colour { Red, Green, Blue } or enum Op { Add = 1, Sub }
//
// The whole enum is built while compiling and stored as a single
// constant, so declaring one costs nothing at run time.
void Compiler::enumDeclaration() {
  consume(TokenType::Identifier, "Expect an enum name.");
  std::string enumName = previous_.lexeme;
  int nameConstant = declareParsedVariable(enumName, true);

  ObjEnum* enumeration = runtime_.newEnum(runtime_.internString(enumName));
  // Rooted for the whole build, because every member allocates.
  runtime_.pushRoot((Obj*)enumeration);

  consume(TokenType::LeftBrace, "Expect '{' before enum members.");
  double nextValue = 0;
  while (!check(TokenType::RightBrace) && !check(TokenType::Eof)) {
    consume(TokenType::Identifier, "Expect an enum member name.");
    std::string memberName = previous_.lexeme;
    double value = nextValue;
    if (match(TokenType::Equal)) {
      bool negative = match(TokenType::Minus);
      consume(TokenType::Number, "Expect a number after '=' in an enum.");
      value = negative ? -previous_.number : previous_.number;
    }
    nextValue = value + 1;

    ObjString* interned = runtime_.internString(memberName);
    Value existing;
    if (enumeration->members.get(interned, &existing)) {
      error("Duplicate enum member '" + memberName + "'.");
    } else {
      ObjEnumMember* member =
          runtime_.newEnumMember(enumeration, interned, value);
      enumeration->members.set(interned, objValue((Obj*)member));
      enumeration->ordered.push_back(objValue((Obj*)member));
    }
    if (!match(TokenType::Comma)) break;
  }
  consume(TokenType::RightBrace, "Expect '}' after enum members.");

  if (enumeration->ordered.empty()) {
    error("An enum needs at least one member.");
  }

  emitByte(OP_CONSTANT);
  emitShort(makeConstant(objValue((Obj*)enumeration)));
  runtime_.popRoot();

  if (state_->scopeDepth == 0) constGlobals_.insert(enumName);
  defineVariable(nameConstant, true);
}

void Compiler::classDeclaration() {
  consume(TokenType::Identifier, "Expect a class name.");
  std::string className = previous_.lexeme;
  int nameConstant = identifierConstant(className);
  declareVariable(className, false);

  emitByte(OP_CLASS);
  emitShort(nameConstant);
  defineVariable(nameConstant, false);

  ClassState classState;
  classState.enclosing = classState_;
  classState_ = &classState;

  if (match(TokenType::Less)) {
    consume(TokenType::Identifier, "Expect a superclass name.");
    variable(false);
    if (className == previous_.lexeme) {
      error("A class cannot inherit from itself.");
    }
    // `super` is resolved as an upvalue, so it needs a scope of its own
    // that outlives the method bodies that capture it.
    beginScope();
    addLocal("super", true);
    markInitialized();

    namedVariable(className, false);
    emitByte(OP_INHERIT);
    classState.hasSuperclass = true;
  }

  namedVariable(className, false);
  consume(TokenType::LeftBrace, "Expect '{' before a class body.");
  while (!check(TokenType::RightBrace) && !check(TokenType::Eof)) {
    method();
  }
  consume(TokenType::RightBrace, "Expect '}' after a class body.");
  emitByte(OP_POP);

  if (classState.hasSuperclass) endScope();
  classState_ = classState.enclosing;
}

void Compiler::method() {
  consume(TokenType::Identifier, "Expect a method name.");
  std::string name = previous_.lexeme;
  int constant = identifierConstant(name);
  FunctionKind kind =
      name == "init" ? FunctionKind::Initializer : FunctionKind::Method;
  function(kind, name);
  emitByte(OP_METHOD);
  emitShort(constant);
}

void Compiler::function(FunctionKind kind, const std::string& name) {
  AllowCalls allowCalls(*this);
  FunctionState state;
  state.enclosing = state_;
  state.kind = kind;
  state.function = runtime_.newFunction(module_);
  // The function is only reachable from a C++ local until it becomes a
  // constant in the enclosing chunk, so it has to be rooted across every
  // allocation the body performs.
  runtime_.pushRoot((Obj*)state.function);
  if (!name.empty()) state.function->name = runtime_.internString(name);

  // Slot zero holds the receiver for methods and the function itself
  // otherwise. Naming it makes `this` resolve like any other local.
  state.locals.push_back(
      {kind == FunctionKind::Function || kind == FunctionKind::Script ? ""
                                                                      : "this",
       0, false, true});
  state_ = &state;
  beginScope();

  consume(TokenType::LeftParen, "Expect '(' after a function name.");
  bool seenOptional = false;
  if (!check(TokenType::RightParen)) {
    do {
      if (match(TokenType::Ellipsis)) {
        // A rest parameter gathers whatever is left, so nothing can
        // follow it.
        int restConstant = parseVariable("Expect a name after '...'.", false);
        state_->function->paramTypes.push_back("");
        defineVariable(restConstant, false);
        state_->function->hasRest = true;
        break;
      }

      state_->function->maxArity++;
      if (state_->function->maxArity > 255) {
        errorAtCurrent("Cannot have more than 255 parameters.");
      }
      int constant = parseVariable("Expect a parameter name.", false);
      std::string annotation = typeAnnotation();
      state_->function->paramTypes.push_back(annotation);
      defineVariable(constant, false);

      if (match(TokenType::Equal)) {
        seenOptional = true;
        // The default is compiled here, which puts it at the top of the
        // body, and it is skipped when the call supplied this argument.
        // Parameters declared earlier are already in scope, so a default
        // can refer to them.
        int index = state_->function->maxArity - 1;
        emitByte(OP_JUMP_IF_ARG);
        emitByte((uint8_t)index);
        emitShort(0xffff);
        int suppliedJump = (int)chunk().code.size() - 2;
        expression();
        emitByte(OP_SET_LOCAL);
        emitByte((uint8_t)(index + 1));
        emitByte(OP_POP);
        patchJump(suppliedJump);
      } else if (seenOptional) {
        error("A required parameter cannot follow one with a default.");
      } else {
        state_->function->arity++;
      }
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightParen, "Expect ')' after parameters.");

  if (match(TokenType::Arrow)) {
    consume(TokenType::Identifier, "Expect a return type name after '->'.");
    state_->function->returnType = previous_.lexeme;
  }

  consume(TokenType::LeftBrace, "Expect '{' before a function body.");
  block();

  emitReturn();
  state.function->slotCount = state.maxLocals + state.maxTemps + 8;
  ObjFunction* function = state.function;
  state_ = state.enclosing;
  runtime_.popRoot();

  emitByte(OP_CLOSURE);
  emitShort(makeConstant(objValue((Obj*)function)));
  for (int i = 0; i < function->upvalueCount; i++) {
    emitByte(state.upvalues[i].isLocal ? 1 : 0);
    emitByte(state.upvalues[i].index);
  }
}

// ---------------------------------------------------------------------
// statements

void Compiler::statement() {
  if (match(TokenType::If)) {
    ifStatement();
  } else if (match(TokenType::While)) {
    whileStatement();
  } else if (match(TokenType::For)) {
    forStatement();
  } else if (match(TokenType::Switch)) {
    switchStatement();
  } else if (match(TokenType::Return)) {
    returnStatement();
  } else if (match(TokenType::Break)) {
    breakStatement();
  } else if (match(TokenType::Continue)) {
    continueStatement();
  } else if (match(TokenType::Try)) {
    tryStatement();
  } else if (match(TokenType::Throw)) {
    throwStatement();
  } else if (match(TokenType::LeftBrace)) {
    beginScope();
    block();
    endScope();
  } else {
    expressionStatement();
  }
}

void Compiler::block() {
  while (!check(TokenType::RightBrace) && !check(TokenType::Eof)) {
    declaration();
  }
  consume(TokenType::RightBrace, "Expect '}' after a block.");
}

void Compiler::expressionStatement() {
  expression();
  consume(TokenType::Semicolon, "Expect ';' after an expression.");
  emitByte(OP_POP);
}

void Compiler::ifStatement() {
  consume(TokenType::LeftParen, "Expect '(' after 'if'.");
  expression();
  consume(TokenType::RightParen, "Expect ')' after a condition.");

  int thenJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  int elseJump = emitJump(OP_JUMP);

  patchJump(thenJump);
  emitByte(OP_POP);
  if (match(TokenType::Else)) statement();
  patchJump(elseJump);
}

void Compiler::whileStatement() {
  int loopStart = (int)chunk().code.size();
  state_->loops.push_back({loopStart, state_->scopeDepth, state_->tryDepth, {}});

  consume(TokenType::LeftParen, "Expect '(' after 'while'.");
  expression();
  consume(TokenType::RightParen, "Expect ')' after a condition.");

  int exitJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  statement();
  emitLoop(loopStart);

  patchJump(exitJump);
  emitByte(OP_POP);

  for (int jump : state_->loops.back().breakJumps) patchJump(jump);
  state_->loops.pop_back();
}

void Compiler::forStatement() {
  beginScope();
  consume(TokenType::LeftParen, "Expect '(' after 'for'.");

  if (match(TokenType::Semicolon)) {
    // No initializer.
  } else if (match(TokenType::Let)) {
    // A pattern here can only belong to a for-in loop.
    if (check(TokenType::LeftBracket) || check(TokenType::LeftBrace)) {
      std::shared_ptr<Pattern> pattern = parsePattern();
      consume(TokenType::In, "Expect 'in' after a for-in pattern.");
      forInStatement("", pattern);
      endScope();
      return;
    }
    // The name has to be read before it is clear which kind of loop this
    // is, because "in" only shows up after it.
    consume(TokenType::Identifier, "Expect a variable name.");
    std::string name = previous_.lexeme;
    if (match(TokenType::In)) {
      forInStatement(name, nullptr);
      endScope();
      return;
    }
    int global = declareParsedVariable(name, false);
    typeAnnotation();
    if (match(TokenType::Equal)) {
      expression();
    } else {
      emitByte(OP_NIL);
    }
    consume(TokenType::Semicolon, "Expect ';' after a variable declaration.");
    defineVariable(global, false);
  } else {
    expressionStatement();
  }

  int loopStart = (int)chunk().code.size();
  int exitJump = -1;
  if (!match(TokenType::Semicolon)) {
    expression();
    consume(TokenType::Semicolon, "Expect ';' after a loop condition.");
    exitJump = emitJump(OP_JUMP_IF_FALSE);
    emitByte(OP_POP);
  }

  // The increment is compiled before the body but has to run after it, so
  // it is jumped over on the way in and jumped back to on the way out.
  int continueTarget = loopStart;
  if (!match(TokenType::RightParen)) {
    int bodyJump = emitJump(OP_JUMP);
    int incrementStart = (int)chunk().code.size();
    expression();
    emitByte(OP_POP);
    consume(TokenType::RightParen, "Expect ')' after for clauses.");
    emitLoop(loopStart);
    loopStart = incrementStart;
    continueTarget = incrementStart;
    patchJump(bodyJump);
  }

  state_->loops.push_back(
      {continueTarget, state_->scopeDepth, state_->tryDepth, {}});
  statement();
  emitLoop(loopStart);

  if (exitJump != -1) {
    patchJump(exitJump);
    emitByte(OP_POP);
  }
  for (int jump : state_->loops.back().breakJumps) patchJump(jump);
  state_->loops.pop_back();
  endScope();
}

// Drops the try handlers opened inside the loop. Leaving a try block by
// jumping out of it still has to close it, or the handler stays live and
// a later throw lands in dead code.
void Compiler::closeLoopHandlers() {
  for (int i = state_->tryDepth; i > state_->loops.back().tryDepth; i--) {
    emitByte(OP_TRY_END);
  }
}

// for (let x in subject) walks an array, a map's keys, or a string's
// characters. Two hidden locals hold the sequence and the position.
void Compiler::forInStatement(const std::string& name,
                              const std::shared_ptr<Pattern>& pattern) {
  expression();
  consume(TokenType::RightParen, "Expect ')' after a for-in subject.");
  emitByte(OP_ITER_PREP);

  // The hidden names contain a space, so no program can reach them.
  addLocal("  seq", true);
  markInitialized();
  int seqSlot = (int)state_->locals.size() - 1;

  emitConstant(numberValue(0));
  addLocal("  idx", true);
  markInitialized();
  int idxSlot = (int)state_->locals.size() - 1;

  int loopStart = (int)chunk().code.size();
  emitByte(OP_ITER_NEXT);
  emitByte((uint8_t)seqSlot);
  emitByte((uint8_t)idxSlot);
  emitShort(0xffff);
  int exitJump = (int)chunk().code.size() - 2;

  state_->loops.push_back(
      {loopStart, state_->scopeDepth, state_->tryDepth, {}});

  // ITER_NEXT leaves the element on top of the stack, which is exactly
  // the slot the loop variable occupies.
  beginScope();
  if (pattern != nullptr) {
    // With a pattern the element goes to a hidden slot and the pattern
    // binds from there.
    int itemSlot = addHiddenLocal("  item");
    emitPattern(*pattern, itemSlot, false);
  } else {
    addLocal(name, false);
    markInitialized();
  }
  statement();
  endScope();

  emitLoop(loopStart);
  patchJump(exitJump);
  for (int jump : state_->loops.back().breakJumps) patchJump(jump);
  state_->loops.pop_back();
}

// Statements belonging to one case, up to the next case or the closing
// brace. There is no fall through, so no break is needed to end a case.
void Compiler::caseBody() {
  beginScope();
  while (!check(TokenType::Case) && !check(TokenType::Default) &&
         !check(TokenType::RightBrace) && !check(TokenType::Eof)) {
    declaration();
  }
  endScope();
}

void Compiler::switchStatement() {
  consume(TokenType::LeftParen, "Expect '(' after 'switch'.");
  expression();
  consume(TokenType::RightParen, "Expect ')' after a switch value.");
  consume(TokenType::LeftBrace, "Expect '{' before switch cases.");

  beginScope();
  // The subject is kept in a hidden local so that each case can compare
  // against it without evaluating it again.
  addLocal("  switch", true);
  markInitialized();
  int valueSlot = (int)state_->locals.size() - 1;

  std::vector<int> endJumps;
  bool sawDefault = false;

  while (!check(TokenType::RightBrace) && !check(TokenType::Eof)) {
    if (match(TokenType::Case)) {
      if (sawDefault) error("A case cannot come after the default clause.");

      std::vector<int> matchJumps;
      do {
        emitByte(OP_GET_LOCAL);
        emitByte((uint8_t)valueSlot);
        expression();
        emitByte(OP_EQUAL);
        matchJumps.push_back(emitJump(OP_JUMP_IF_TRUE));
        // This test failed, so drop its result and try the next value.
        emitByte(OP_POP);
      } while (match(TokenType::Comma));
      consume(TokenType::Colon, "Expect ':' after case values.");

      // Every value failed, so skip the body.
      int skipJump = emitJump(OP_JUMP);
      for (int jump : matchJumps) patchJump(jump);
      // Exactly one test left a true behind. Drop it.
      emitByte(OP_POP);
      caseBody();
      endJumps.push_back(emitJump(OP_JUMP));
      patchJump(skipJump);
    } else if (match(TokenType::Default)) {
      if (sawDefault) error("A switch can only have one default clause.");
      sawDefault = true;
      consume(TokenType::Colon, "Expect ':' after 'default'.");
      caseBody();
      endJumps.push_back(emitJump(OP_JUMP));
    } else {
      errorAtCurrent("Expect 'case' or 'default' in a switch body.");
      break;
    }
  }

  consume(TokenType::RightBrace, "Expect '}' after switch cases.");
  for (int jump : endJumps) patchJump(jump);
  endScope();
}

void Compiler::popLoopLocals(int targetDepth) {
  for (int i = (int)state_->locals.size() - 1; i >= 0; i--) {
    if (state_->locals[(size_t)i].depth <= targetDepth) break;
    emitByte(state_->locals[(size_t)i].isCaptured ? OP_CLOSE_UPVALUE : OP_POP);
  }
}

void Compiler::breakStatement() {
  if (state_->loops.empty()) {
    error("Cannot use 'break' outside a loop.");
    return;
  }
  consume(TokenType::Semicolon, "Expect ';' after 'break'.");
  closeLoopHandlers();
  popLoopLocals(state_->loops.back().scopeDepth);
  state_->loops.back().breakJumps.push_back(emitJump(OP_JUMP));
}

void Compiler::continueStatement() {
  if (state_->loops.empty()) {
    error("Cannot use 'continue' outside a loop.");
    return;
  }
  consume(TokenType::Semicolon, "Expect ';' after 'continue'.");
  closeLoopHandlers();
  popLoopLocals(state_->loops.back().scopeDepth);
  emitLoop(state_->loops.back().continueTarget);
}

void Compiler::returnStatement() {
  if (state_->kind == FunctionKind::Script) {
    error("Cannot return from top level code.");
  }
  if (match(TokenType::Semicolon)) {
    emitReturn();
    return;
  }
  if (state_->kind == FunctionKind::Initializer) {
    error("Cannot return a value from an initializer.");
  }
  expression();
  consume(TokenType::Semicolon, "Expect ';' after a return value.");
  emitByte(OP_RETURN);
}

void Compiler::tryStatement() {
  int handlerJump = emitJump(OP_TRY_BEGIN);
  state_->tryDepth++;
  beginScope();
  consume(TokenType::LeftBrace, "Expect '{' after 'try'.");
  block();
  endScope();
  state_->tryDepth--;
  emitByte(OP_TRY_END);
  int doneJump = emitJump(OP_JUMP);

  // Control arrives here with the stack cut back to its depth at
  // TRY_BEGIN and the error value pushed on top. It goes into a hidden
  // slot so that every clause can look at the same error.
  patchJump(handlerJump);
  beginScope();
  int errorSlot = addHiddenLocal("  error");

  std::vector<int> clauseDone;
  bool sawCatchAll = false;
  bool sawClause = false;

  while (check(TokenType::Catch)) {
    if (sawCatchAll) {
      error("A catch clause cannot follow the one with no filter.");
    }
    advance();
    sawClause = true;

    consume(TokenType::LeftParen, "Expect '(' after 'catch'.");
    consume(TokenType::Identifier, "Expect an error variable name.");
    std::string name = previous_.lexeme;

    int skipJump = -1;
    if (match(TokenType::Colon)) {
      // A filter is an ordinary expression, so it can be a string kind, a
      // class, or anything that produces one.
      emitByte(OP_GET_LOCAL);
      emitByte((uint8_t)errorSlot);
      expression();
      emitByte(OP_CATCH_MATCHES);
      skipJump = emitJump(OP_JUMP_IF_FALSE);
      emitByte(OP_POP);
    } else {
      sawCatchAll = true;
    }
    consume(TokenType::RightParen, "Expect ')' after the error variable.");
    consume(TokenType::LeftBrace, "Expect '{' before a catch block.");

    beginScope();
    emitByte(OP_GET_LOCAL);
    emitByte((uint8_t)errorSlot);
    addLocal(name, false);
    markInitialized();
    block();
    endScope();
    clauseDone.push_back(emitJump(OP_JUMP));

    if (skipJump != -1) {
      patchJump(skipJump);
      emitByte(OP_POP);
    }
  }

  if (!sawClause) {
    errorAtCurrent("Expect 'catch' after a try block.");
  }

  if (!sawCatchAll) {
    // No clause matched, so the error carries on outwards unchanged.
    emitByte(OP_GET_LOCAL);
    emitByte((uint8_t)errorSlot);
    emitByte(OP_THROW);
  }

  for (int jump : clauseDone) patchJump(jump);
  // Drops the hidden error. The ordinary path jumps past this.
  endScope();
  patchJump(doneJump);
}

void Compiler::throwStatement() {
  expression();
  consume(TokenType::Semicolon, "Expect ';' after a thrown value.");
  emitByte(OP_THROW);
}

// ---------------------------------------------------------------------
// expressions

void Compiler::expression() { parsePrecedence(Precedence::Assignment); }

void Compiler::parsePrecedence(Precedence precedence) {
  // Each level of nesting can leave a couple of values pending on the
  // stack, so the deepest nesting bounds what an expression costs.
  state_->nestDepth++;
  noteTemps(state_->nestDepth * 2);
  struct DepthGuard {
    FunctionState* state;
    ~DepthGuard() { state->nestDepth--; }
  } depthGuard{state_};

  advance();
  ParseFn prefixRule = getRule(previous_.type)->prefix;
  if (prefixRule == nullptr) {
    error("Expect an expression.");
    return;
  }

  bool canAssign = precedence <= Precedence::Assignment;
  (this->*prefixRule)(canAssign);

  for (;;) {
    // While parsing the callee of `spawn`, stop before the argument list
    // so that the spawn form can consume it itself.
    if (suppressCall_ && check(TokenType::LeftParen)) break;
    if (precedence > getRule(current_.type)->precedence) break;
    advance();
    ParseFn infixRule = getRule(previous_.type)->infix;
    (this->*infixRule)(canAssign);
  }

  if (canAssign && match(TokenType::Equal)) {
    error("Invalid assignment target.");
  }
}

uint8_t Compiler::argumentList() {
  AllowCalls allowCalls(*this);
  uint8_t count = 0;
  if (!check(TokenType::RightParen)) {
    do {
      expression();
      if (count == 255) error("Cannot pass more than 255 arguments.");
      count++;
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightParen, "Expect ')' after arguments.");
  noteTemps(count + 2);
  return count;
}

bool Compiler::matchCompound(uint8_t* op) {
  if (match(TokenType::PlusEqual)) { *op = OP_ADD; return true; }
  if (match(TokenType::MinusEqual)) { *op = OP_SUBTRACT; return true; }
  if (match(TokenType::StarEqual)) { *op = OP_MULTIPLY; return true; }
  if (match(TokenType::SlashEqual)) { *op = OP_DIVIDE; return true; }
  if (match(TokenType::PercentEqual)) { *op = OP_MODULO; return true; }
  return false;
}

void Compiler::grouping(bool) {
  AllowCalls allowCalls(*this);
  expression();
  consume(TokenType::RightParen, "Expect ')' after an expression.");
}

void Compiler::number(bool) { emitConstant(numberValue(previous_.number)); }

void Compiler::stringLiteral(bool) {
  emitConstant(objValue((Obj*)runtime_.internString(previous_.text)));
}

void Compiler::interpolation(bool) {
  AllowCalls allowCalls(*this);
  // "a${x}b" compiles to the same code as "a" + str(x) + "b". The literal
  // parts are emitted even when empty so the result is always a string.
  emitConstant(objValue((Obj*)runtime_.internString(previous_.text)));
  for (;;) {
    expression();
    emitByte(OP_TO_STRING);
    emitByte(OP_ADD);

    if (match(TokenType::StringInterp)) {
      emitConstant(objValue((Obj*)runtime_.internString(previous_.text)));
      emitByte(OP_ADD);
      continue;
    }
    if (match(TokenType::String)) {
      emitConstant(objValue((Obj*)runtime_.internString(previous_.text)));
      emitByte(OP_ADD);
      break;
    }
    errorAtCurrent("Unterminated string interpolation.");
    break;
  }
}

void Compiler::literal(bool) {
  switch (previous_.type) {
    case TokenType::False: emitByte(OP_FALSE); break;
    case TokenType::Nil: emitByte(OP_NIL); break;
    case TokenType::True: emitByte(OP_TRUE); break;
    default: break;
  }
}

void Compiler::variable(bool canAssign) {
  namedVariable(previous_.lexeme, canAssign);
}

void Compiler::unary(bool) {
  TokenType op = previous_.type;
  parsePrecedence(Precedence::Unary);
  switch (op) {
    case TokenType::Bang: emitByte(OP_NOT); break;
    case TokenType::Minus: emitByte(OP_NEGATE); break;
    case TokenType::Tilde: emitByte(OP_BIT_NOT); break;
    default: break;
  }
}

void Compiler::binary(bool) {
  TokenType op = previous_.type;
  const ParseRule* rule = getRule(op);
  parsePrecedence((Precedence)((int)rule->precedence + 1));

  switch (op) {
    case TokenType::BangEqual: emitByte(OP_NOT_EQUAL); break;
    case TokenType::EqualEqual: emitByte(OP_EQUAL); break;
    case TokenType::Greater: emitByte(OP_GREATER); break;
    case TokenType::GreaterEqual: emitByte(OP_GREATER_EQUAL); break;
    case TokenType::Less: emitByte(OP_LESS); break;
    case TokenType::LessEqual: emitByte(OP_LESS_EQUAL); break;
    case TokenType::Plus: emitByte(OP_ADD); break;
    case TokenType::Minus: emitByte(OP_SUBTRACT); break;
    case TokenType::Star: emitByte(OP_MULTIPLY); break;
    case TokenType::Slash: emitByte(OP_DIVIDE); break;
    case TokenType::Percent: emitByte(OP_MODULO); break;
    case TokenType::Ampersand: emitByte(OP_BIT_AND); break;
    case TokenType::Pipe: emitByte(OP_BIT_OR); break;
    case TokenType::Caret: emitByte(OP_BIT_XOR); break;
    case TokenType::LessLess: emitByte(OP_SHIFT_LEFT); break;
    case TokenType::GreaterGreater: emitByte(OP_SHIFT_RIGHT); break;
    default: break;
  }
}

void Compiler::call(bool) {
  uint8_t argCount = argumentList();
  emitBytes(OP_CALL, argCount);
}

void Compiler::dot(bool canAssign) {
  consume(TokenType::Identifier, "Expect a property name after '.'.");
  int name = identifierConstant(previous_.lexeme);

  uint8_t compound;
  if (canAssign && match(TokenType::Equal)) {
    expression();
    emitByte(OP_SET_PROPERTY);
    emitShort(name);
  } else if (canAssign && matchCompound(&compound)) {
    // The receiver is needed twice, once to read the property and once to
    // write it back, so it is duplicated rather than evaluated twice.
    emitByte(OP_DUP);
    emitByte(OP_GET_PROPERTY);
    emitShort(name);
    expression();
    emitByte(compound);
    emitByte(OP_SET_PROPERTY);
    emitShort(name);
  } else if (!suppressCall_ && match(TokenType::LeftParen)) {
    // Fusing the lookup and the call saves allocating a bound method for
    // the common `obj.method(...)` shape.
    uint8_t argCount = argumentList();
    emitByte(OP_INVOKE);
    emitShort(name);
    emitByte(argCount);
  } else {
    emitByte(OP_GET_PROPERTY);
    emitShort(name);
  }
}

void Compiler::index(bool canAssign) {
  AllowCalls allowCalls(*this);
  expression();
  consume(TokenType::RightBracket, "Expect ']' after an index.");
  uint8_t compound;
  if (canAssign && match(TokenType::Equal)) {
    expression();
    emitByte(OP_SET_INDEX);
  } else if (canAssign && matchCompound(&compound)) {
    // Both the target and the index are needed twice.
    emitByte(OP_DUP2);
    emitByte(OP_GET_INDEX);
    expression();
    emitByte(compound);
    emitByte(OP_SET_INDEX);
  } else {
    emitByte(OP_GET_INDEX);
  }
}

void Compiler::andOp(bool) {
  int endJump = emitJump(OP_JUMP_IF_FALSE);
  emitByte(OP_POP);
  parsePrecedence(Precedence::And);
  patchJump(endJump);
}

void Compiler::orOp(bool) {
  int endJump = emitJump(OP_JUMP_IF_TRUE);
  emitByte(OP_POP);
  parsePrecedence(Precedence::Or);
  patchJump(endJump);
}

void Compiler::arrayLiteral(bool) {
  AllowCalls allowCalls(*this);
  int count = 0;
  if (!check(TokenType::RightBracket)) {
    do {
      if (check(TokenType::RightBracket)) break;  // allow a trailing comma
      expression();
      count++;
      if (count > 0xffff) error("Too many elements in an array literal.");
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightBracket, "Expect ']' after array elements.");
  noteTemps(count + 2);
  emitByte(OP_ARRAY);
  emitShort(count);
}

void Compiler::mapLiteral(bool) {
  AllowCalls allowCalls(*this);
  int count = 0;
  if (!check(TokenType::RightBrace)) {
    do {
      if (check(TokenType::RightBrace)) break;  // allow a trailing comma
      expression();
      consume(TokenType::Colon, "Expect ':' after a map key.");
      expression();
      count++;
      if (count > 0xffff) error("Too many entries in a map literal.");
    } while (match(TokenType::Comma));
  }
  consume(TokenType::RightBrace, "Expect '}' after map entries.");
  noteTemps(count * 2 + 2);
  emitByte(OP_MAP);
  emitShort(count);
}

void Compiler::lambda(bool) { function(FunctionKind::Function, ""); }

void Compiler::thisExpr(bool) {
  if (classState_ == nullptr) {
    error("Cannot use 'this' outside a class.");
    return;
  }
  variable(false);
}

void Compiler::superExpr(bool) {
  if (classState_ == nullptr) {
    error("Cannot use 'super' outside a class.");
  } else if (!classState_->hasSuperclass) {
    error("Cannot use 'super' in a class with no superclass.");
  }
  consume(TokenType::Dot, "Expect '.' after 'super'.");
  consume(TokenType::Identifier, "Expect a superclass method name.");
  int name = identifierConstant(previous_.lexeme);

  namedVariable("this", false);
  if (!suppressCall_ && match(TokenType::LeftParen)) {
    uint8_t argCount = argumentList();
    namedVariable("super", false);
    emitByte(OP_SUPER_INVOKE);
    emitShort(name);
    emitByte(argCount);
  } else {
    namedVariable("super", false);
    emitByte(OP_GET_SUPER);
    emitShort(name);
  }
}

void Compiler::spawnExpr(bool) {
  // The callee is parsed with calls suppressed, so `spawn worker(ch)`
  // leaves the argument list here rather than compiling a normal call.
  bool previousSuppress = suppressCall_;
  suppressCall_ = true;
  parsePrecedence(Precedence::Call);
  suppressCall_ = previousSuppress;

  consume(TokenType::LeftParen, "Expect '(' after a spawn target.");
  uint8_t argCount = argumentList();
  emitBytes(OP_SPAWN, argCount);
}

// ---------------------------------------------------------------------

const ParseRule* Compiler::getRule(TokenType type) {
  static const ParseRule rules[] = {
      /* LeftParen    */ {&Compiler::grouping, &Compiler::call, Precedence::Call},
      /* RightParen   */ {nullptr, nullptr, Precedence::None},
      /* LeftBrace    */ {&Compiler::mapLiteral, nullptr, Precedence::None},
      /* RightBrace   */ {nullptr, nullptr, Precedence::None},
      /* LeftBracket  */ {&Compiler::arrayLiteral, &Compiler::index, Precedence::Call},
      /* RightBracket */ {nullptr, nullptr, Precedence::None},
      /* Comma        */ {nullptr, nullptr, Precedence::None},
      /* Dot          */ {nullptr, &Compiler::dot, Precedence::Call},
      /* Minus        */ {&Compiler::unary, &Compiler::binary, Precedence::Term},
      /* Plus         */ {nullptr, &Compiler::binary, Precedence::Term},
      /* Semicolon    */ {nullptr, nullptr, Precedence::None},
      /* Slash        */ {nullptr, &Compiler::binary, Precedence::Factor},
      /* Star         */ {nullptr, &Compiler::binary, Precedence::Factor},
      /* Percent      */ {nullptr, &Compiler::binary, Precedence::Factor},
      /* Colon        */ {nullptr, nullptr, Precedence::None},
      /* Arrow        */ {nullptr, nullptr, Precedence::None},
      /* Bang         */ {&Compiler::unary, nullptr, Precedence::None},
      /* BangEqual    */ {nullptr, &Compiler::binary, Precedence::Equality},
      /* Equal        */ {nullptr, nullptr, Precedence::None},
      /* EqualEqual   */ {nullptr, &Compiler::binary, Precedence::Equality},
      /* Greater      */ {nullptr, &Compiler::binary, Precedence::Comparison},
      /* GreaterEqual */ {nullptr, &Compiler::binary, Precedence::Comparison},
      /* Less         */ {nullptr, &Compiler::binary, Precedence::Comparison},
      /* LessEqual    */ {nullptr, &Compiler::binary, Precedence::Comparison},
      /* Identifier   */ {&Compiler::variable, nullptr, Precedence::None},
      /* String       */ {&Compiler::stringLiteral, nullptr, Precedence::None},
      /* StringInterp */ {&Compiler::interpolation, nullptr, Precedence::None},
      /* Number       */ {&Compiler::number, nullptr, Precedence::None},
      /* And          */ {nullptr, &Compiler::andOp, Precedence::And},
      /* Break        */ {nullptr, nullptr, Precedence::None},
      /* Catch        */ {nullptr, nullptr, Precedence::None},
      /* Class        */ {nullptr, nullptr, Precedence::None},
      /* Const        */ {nullptr, nullptr, Precedence::None},
      /* Continue     */ {nullptr, nullptr, Precedence::None},
      /* Else         */ {nullptr, nullptr, Precedence::None},
      /* False        */ {&Compiler::literal, nullptr, Precedence::None},
      /* For          */ {nullptr, nullptr, Precedence::None},
      /* Fun          */ {&Compiler::lambda, nullptr, Precedence::None},
      /* If           */ {nullptr, nullptr, Precedence::None},
      /* Import       */ {nullptr, nullptr, Precedence::None},
      /* Let          */ {nullptr, nullptr, Precedence::None},
      /* Nil          */ {&Compiler::literal, nullptr, Precedence::None},
      /* Or           */ {nullptr, &Compiler::orOp, Precedence::Or},
      /* Return       */ {nullptr, nullptr, Precedence::None},
      /* Spawn        */ {&Compiler::spawnExpr, nullptr, Precedence::None},
      /* Super        */ {&Compiler::superExpr, nullptr, Precedence::None},
      /* This         */ {&Compiler::thisExpr, nullptr, Precedence::None},
      /* Throw        */ {nullptr, nullptr, Precedence::None},
      /* True         */ {&Compiler::literal, nullptr, Precedence::None},
      /* Try          */ {nullptr, nullptr, Precedence::None},
      /* While        */ {nullptr, nullptr, Precedence::None},
      /* As           */ {nullptr, nullptr, Precedence::None},
      /* Ampersand    */ {nullptr, &Compiler::binary, Precedence::BitAnd},
      /* Pipe         */ {nullptr, &Compiler::binary, Precedence::BitOr},
      /* Caret        */ {nullptr, &Compiler::binary, Precedence::BitXor},
      /* Tilde        */ {&Compiler::unary, nullptr, Precedence::None},
      /* LessLess     */ {nullptr, &Compiler::binary, Precedence::Shift},
      /* GreaterGreater */ {nullptr, &Compiler::binary, Precedence::Shift},
      /* PlusEqual    */ {nullptr, nullptr, Precedence::None},
      /* MinusEqual   */ {nullptr, nullptr, Precedence::None},
      /* StarEqual    */ {nullptr, nullptr, Precedence::None},
      /* SlashEqual   */ {nullptr, nullptr, Precedence::None},
      /* PercentEqual */ {nullptr, nullptr, Precedence::None},
      /* In           */ {nullptr, nullptr, Precedence::None},
      /* Switch       */ {nullptr, nullptr, Precedence::None},
      /* Case         */ {nullptr, nullptr, Precedence::None},
      /* Default      */ {nullptr, nullptr, Precedence::None},
      /* Ellipsis     */ {nullptr, nullptr, Precedence::None},
      /* Enum         */ {nullptr, nullptr, Precedence::None},
      /* Error        */ {nullptr, nullptr, Precedence::None},
      /* Eof          */ {nullptr, nullptr, Precedence::None},
  };
  static_assert(sizeof(rules) / sizeof(ParseRule) ==
                    (size_t)TokenType::Eof + 1,
                "Parse rule table must cover every token type.");
  return &rules[(size_t)type];
}

ObjFunction* Compiler::compileScript() {
  FunctionState state;
  state.kind = FunctionKind::Script;
  state.function = runtime_.newFunction(module_);
  runtime_.pushRoot((Obj*)state.function);
  state.locals.push_back({"", 0, false, true});
  state_ = &state;

  advance();
  while (!match(TokenType::Eof)) {
    declaration();
  }
  emitReturn();
  state.function->slotCount = state.maxLocals + state.maxTemps + 8;

  runtime_.popRoot();
  state_ = nullptr;
  return hadError_ ? nullptr : state.function;
}

ObjFunction* compile(Runtime& runtime, const std::string& source,
                     ObjModule* module, bool quiet) {
  Compiler compiler(runtime, source, module, quiet);
  return compiler.compileScript();
}

}  // namespace red
