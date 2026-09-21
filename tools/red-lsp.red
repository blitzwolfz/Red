// A language server for Red, written in Red.
//
//   red tools/red-lsp.red
//
// Speaks the Language Server Protocol over standard input and output, so
// any editor that can start a command and talk LSP to it will do. See
// editors/README.md for the settings.
//
// What it does:
//
//   diagnostics       the compiler's own errors, as you type
//   document symbols  functions, classes, enums and top level bindings
//   go to definition  within the file
//   hover             the declaring line, or what a builtin is for
//   completion        the builtins, and everything the file declares
//
// What it does not do: anything that needs to know types, or to follow a
// name into another file. Both want a compiler that hands back a tree,
// and Red's throws one away as it goes.

import "json.red" as json;

// ---------------------------------------------------------------------
// The protocol

// Reads one message. The header is lines of "Name: value" ending in a
// blank line, and only the length matters.
fun readMessage(input) {
  let length = -1;
  for (;;) {
    const line = input.read_line();
    if (line == nil) { return nil; }
    const trimmed = line.trim();
    if (trimmed == "") { break; }
    const colon = trimmed.find(":");
    if (colon < 0) { continue; }
    const name = trimmed.sub(0, colon).trim().lower();
    if (name == "content-length") {
      length = num(trimmed.sub(colon + 1).trim());
    }
  }
  if (length == nil or length < 0) { return nil; }
  const body = input.read(length);
  if (body == nil) { return nil; }
  return json.parse(body);
}

fun writeMessage(output, message) {
  const body = json.stringify(message);
  output.write("Content-Length: ${body.len()}\r\n\r\n");
  output.write(body);
  output.flush();
}

fun respond(output, id, result) {
  writeMessage(output, {"jsonrpc": "2.0", "id": id, "result": result});
}

fun notify(output, method, params) {
  writeMessage(output, {"jsonrpc": "2.0", "method": method, "params": params});
}

// ---------------------------------------------------------------------
// Positions
//
// LSP counts lines and characters from zero. Red counts bytes. Nothing
// here needs to be exact about characters beyond the basic plane, so a
// character is a UTF-8 character and the conversion walks the line.

fun splitLines(text) { return text.split("\n"); }

fun position(line, character) {
  return {"line": line, "character": character};
}

// Named makeRange rather than range, because range() is a builtin and a
// module level function of the same name would hide it for the whole
// file.
fun makeRange(startLine, startChar, endLine, endChar) {
  return {"start": position(startLine, startChar),
    "end": position(endLine, endChar)};
}

// The whole of one line, as a range.
fun lineRange(lines, index) {
  let width = 0;
  if (index >= 0 and index < lines.len()) {
    width = lines[index].char_len();
  }
  return makeRange(index, 0, index, width);
}

// ---------------------------------------------------------------------
// Diagnostics
//
// The compiler is the authority on what is wrong with a file, so the
// buffer is written out and compiled. Nothing else here has to know the
// grammar, which is the point: a second opinion about syntax would drift
// from the first one.

const ERROR_LINE = regex("^\\[.*? line (\\d+)\\] Error(?: at '(.*)')?: (.*)$");

fun diagnosticsFor(text) {
  const scratch = env("TMPDIR", "/tmp") + "/red-lsp-${rand(1000000)}.red";
  write_file(scratch, text);
  const result = run([exe_path(), "compile", scratch, "-o", scratch + "c"]);
  remove_file(scratch);
  remove_file(scratch + "c");

  const out = [];
  if (result["code"] == 0) { return out; }

  const lines = splitLines(text);
  for (let line in splitLines(result["err"])) {
    const found = ERROR_LINE.find(line.trim());
    if (found == nil) { continue; }
    const at = num(found["groups"][0]) - 1;
    const near = found["groups"][1];
    let message = found["groups"][2];
    if (near != nil) { message = "${message} (at '${near}')"; }
    out.push({"range": lineRange(lines, at),
        "severity": 1,
        "source": "red",
        "message": message});
  }
  return out;
}

// ---------------------------------------------------------------------
// Symbols
//
// Found with patterns rather than by parsing. A language server that is
// wrong about a symbol now and then is worth having; one that waits for
// a parser that does not exist is not.

const DECLARATIONS = [
  [regex("^\\s*fun\\s+([A-Za-z_][A-Za-z_0-9]*)"), 12],                  // Function
  [regex("^\\s*class\\s+([A-Za-z_][A-Za-z_0-9]*)"), 5],                 // Class
  [regex("^\\s*enum\\s+([A-Za-z_][A-Za-z_0-9]*)"), 10],                 // Enum
  [regex("^(?:const|let)\\s+([A-Za-z_][A-Za-z_0-9]*)"), 13],            // Variable
  [regex("^\\s{2,}([A-Za-z_][A-Za-z_0-9]*)\\s*\\([^)]*\\)\\s*\\{"), 6], // Method
];

class Symbol {
  init(name, kind, line, text) {
    this.name = name;
    this.kind = kind;
    this.line = line;
    this.text = text;
  }
}

fun symbolsIn(text) {
  const out = [];
  const lines = splitLines(text);
  for (let i in range(0, lines.len())) {
    const line = lines[i];
    if (line.trim().starts_with("//")) { continue; }
    for (let [pattern, kind] in DECLARATIONS) {
      const found = pattern.find(line);
      if (found == nil) { continue; }
      out.push(Symbol(found["groups"][0], kind, i, line.trim()));
      break;
    }
  }
  return out;
}

// The word the cursor is inside, or "".
const WORD = regex("[A-Za-z_][A-Za-z_0-9]*");

fun wordAt(text, lineIndex, character) {
  const lines = splitLines(text);
  if (lineIndex < 0 or lineIndex >= lines.len()) { return ""; }
  const line = lines[lineIndex];
  for (let found in WORD.find_all(line)) {
    // The end is exclusive, but a cursor sitting just after a word is
    // still on it, which is what an editor means by "the word here".
    if (character >= found["start"] and character <= found["end"]) {
      return found["text"];
    }
  }
  return "";
}

// ---------------------------------------------------------------------
// What the builtins are for, for hover and completion.

fun builtinHelp() {
  return {
    "print": "print(...) writes its arguments separated by spaces, then a newline",
    "eprint": "eprint(...) the same, on the error stream",
    "write": "write(...) writes with no separator and no newline",
    "len": "len(value) length of a string, array, map, set or enum",
    "str": "str(value) the value as a string",
    "repr": "repr(value) the same, with strings quoted",
    "num": "num(value) a number, or nil when the whole string is not one",
    "int": "int(value) the number with its fractional part removed",
    "type": "type(value) the type name, as a string",
    "chr": "chr(code) a one byte string, 0 to 255",
    "char": "char(code) a one character string, up to 10ffff",
    "range": "range(stop) or range(start, stop[, step])",
    "set": "set() or set(source) builds a set",
    "error": "error(message[, payload[, kind]]) builds an error value",
    "assert": "assert(condition[, message])",
    "regex": "regex(pattern[, flags]) compiles a pattern; flags are i, m, s",
    "run": "run(argv[, input]) runs a program; gives code, out and err",
    "shell": "shell(command[, input]) runs the text through /bin/sh",
    "which": "which(name) where a program is, or nil",
    "read_file": "read_file(path) the whole file as a string, or nil",
    "write_file": "write_file(path, text) writes, replacing the file",
    "list_dir": "list_dir(path) the names inside, sorted, or nil",
    "mkdir": "mkdir(path) makes it, and any parent it needs",
    "open": "open(path[, mode]) a file handle; mode defaults to \"r\"",
    "date": "date([seconds[, utc]]) the parts of a moment, as a map",
    "format_time": "format_time(seconds, pattern[, utc]) strftime patterns",
    "rand": "rand(), rand(stop) or rand(start, stop)",
    "rand_seed": "rand_seed(n) fixes the sequence",
    "round": "round(n) nearest whole number; a half goes away from zero",
    "floor": "floor(n)", "ceil": "ceil(n)", "abs": "abs(n)",
    "sqrt": "sqrt(n)", "pow": "pow(base, exponent)", "exp": "exp(n)",
    "log": "log(n) natural, or log(n, base)", "sign": "sign(n)",
    "min": "min(...)", "max": "max(...)", "hypot": "hypot(a, b)",
    "chan": "chan([capacity]) a channel; 0 or none means unbuffered",
    "sleep": "sleep(seconds) pauses this task only",
    "source_dir": "source_dir() the directory of the file this is written in",
    "source_path": "source_path() the path of that file",
    "library_paths": "library_paths() where import and ffi_open look",
    "exe_path": "exe_path() where this interpreter is",
    "args": "args() the arguments after the program name",
    "env": "env(name[, fallback]) an environment variable",
    "exit": "exit([code]) stops the program at once",
    "collect": "collect() runs a collection now",
    "gc_info": "gc_info() bytes, next, collections and peak",
    "ffi_open": "ffi_open(path) loads a shared library",
  };
}

const KEYWORDS = [
  "and", "as", "break", "case", "catch", "class", "const", "continue",
  "default", "else", "enum", "false", "finally", "for", "fun", "if",
  "import", "in", "is", "let", "nil", "or", "return", "spawn", "super",
  "switch", "this", "throw", "true", "try", "while",
];

// The type names that are always in scope. A class or an enum is a type
// too, but those are named by the program rather than by the language.
const TYPE_NAMES = [
  "Any", "Array", "Bool", "Error", "Fun", "Int", "Map", "Nil", "Num",
  "Set", "String",
];

// ---------------------------------------------------------------------
// The server

class Server {
  init() {
    this.documents = {};
    this.help = builtinHelp();
    this.running = true;
  }

  textOf(uri) { return this.documents.get(uri, ""); }

  publish(output, uri) {
    notify(output, "textDocument/publishDiagnostics",
      {"uri": uri, "diagnostics": diagnosticsFor(this.textOf(uri))});
  }

  onInitialize(output, id) {
    respond(output, id, {
        "capabilities": {
          // Whole documents rather than incremental edits: a file that a
          // person is editing is small, and re-sending it is simpler than
          // applying ranges correctly.
          "textDocumentSync": 1,
          "documentSymbolProvider": true,
          "definitionProvider": true,
          "hoverProvider": true,
          "completionProvider": {"triggerCharacters": ["."]},
        },
        "serverInfo": {"name": "red-lsp", "version": "1"},
      });
  }

  onDocumentSymbol(output, id, uri) {
    const out = [];
    for (let symbol in symbolsIn(this.textOf(uri))) {
      const where = lineRange(splitLines(this.textOf(uri)), symbol.line);
      out.push({"name": symbol.name, "kind": symbol.kind,
          "range": where, "selectionRange": where});
    }
    respond(output, id, out);
  }

  onDefinition(output, id, uri, line, character) {
    const text = this.textOf(uri);
    const word = wordAt(text, line, character);
    for (let symbol in symbolsIn(text)) {
      if (symbol.name != word) { continue; }
      respond(output, id,
        {"uri": uri,
          "range": lineRange(splitLines(text), symbol.line)});
      return;
    }
    respond(output, id, nil);
  }

  onHover(output, id, uri, line, character) {
    const text = this.textOf(uri);
    const word = wordAt(text, line, character);
    if (word == "") {
      respond(output, id, nil);
      return;
    }

    let shown = nil;
    for (let symbol in symbolsIn(text)) {
      if (symbol.name == word) { shown = symbol.text; }
    }
    if (shown == nil) { shown = this.help.get(word, nil); }
    if (shown == nil and KEYWORDS.contains(word)) {
      shown = "${word} is a keyword";
    }
    if (shown == nil and TYPE_NAMES.contains(word)) {
      shown = "${word} is a type";
    }
    if (shown == nil) {
      respond(output, id, nil);
      return;
    }
    respond(output, id,
      {"contents": {"kind": "markdown",
          "value": "```red\n${shown}\n```"}});
  }

  onCompletion(output, id, uri) {
    const out = [];
    const seen = set();
    for (let symbol in symbolsIn(this.textOf(uri))) {
      if (seen.has(symbol.name)) { continue; }
      seen.add(symbol.name);
      out.push({"label": symbol.name, "kind": 3, "detail": symbol.text});
    }
    for (let [name, help] in this.help.entries()) {
      if (seen.has(name)) { continue; }
      seen.add(name);
      out.push({"label": name, "kind": 3, "detail": help});
    }
    for (let word in KEYWORDS) {
      if (seen.has(word)) { continue; }
      out.push({"label": word, "kind": 14});
    }
    for (let name in TYPE_NAMES) {
      if (seen.has(name)) { continue; }
      seen.add(name);
      // 25 is a type parameter in the protocol's list, which is the
      // closest thing it has to "a type".
      out.push({"label": name, "kind": 25});
    }
    respond(output, id, out);
  }

  handle(output, message) {
    const method = message.get("method", nil);
    if (method == nil) { return; }
    const id = message.get("id", nil);
    const params = message.get("params", {});

    // A document's uri, when the message is about one.
    let uri = "";
    const document = params.get("textDocument", nil);
    if (document != nil) { uri = document.get("uri", ""); }

    let line = 0;
    let character = 0;
    const where = params.get("position", nil);
    if (where != nil) {
      line = where.get("line", 0);
      character = where.get("character", 0);
    }

    switch (method) {
      case "initialize": this.onInitialize(output, id);
      case "initialized": nil;
      case "shutdown": respond(output, id, nil);
      case "exit": this.running = false;
      case "textDocument/didOpen": {
        this.documents.set(uri, document.get("text", ""));
        this.publish(output, uri);
      }
      case "textDocument/didChange": {
        const changes = params.get("contentChanges", []);
        if (changes.len() > 0) {
          this.documents.set(uri, changes[changes.len() - 1].get("text", ""));
        }
        this.publish(output, uri);
      }
      case "textDocument/didSave": this.publish(output, uri);
      case "textDocument/didClose": {
        this.documents.remove(uri);
        notify(output, "textDocument/publishDiagnostics",
          {"uri": uri, "diagnostics": []});
      }
      case "textDocument/documentSymbol": this.onDocumentSymbol(output, id, uri);
      case "textDocument/definition": this.onDefinition(output, id, uri, line, character);
      case "textDocument/hover": this.onHover(output, id, uri, line, character);
      case "textDocument/completion": this.onCompletion(output, id, uri);
      default: {
        // A request nobody answered still needs an answer, or the editor
        // waits for one for ever.
        if (id != nil) { respond(output, id, nil); }
      }
    }
  }
}

fun main() {
  const input = open("/dev/stdin", "r");
  const output = open("/dev/stdout", "w");
  const server = Server();

  while (server.running) {
    let message = nil;
    try {
      message = readMessage(input);
    } catch (e: "json") {
      // A message that is not JSON is not something to recover from
      // halfway; stop rather than answer nonsense.
      break;
    }
    if (message == nil) { break; }
    server.handle(output, message);
  }

  input.close();
  output.close();
  return 0;
}

const status = main();
if (status != 0) { exit(status); }
