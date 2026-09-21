# Red v1

This is the original Red: a tree-walking interpreter written in Java. It
is kept here unchanged. It still builds and it still runs.

It is not the current implementation. The current one is in `src/`, and
it is described in [../README.md](../README.md).

## Why it is still here

Two reasons.

It shows where the project started. v1 was a scanner, a parser, a
resolver and an evaluator that walked a syntax tree. v2 keeps the front
end idea and replaces everything behind it with a bytecode compiler, a
stack machine and a garbage collector.

It still runs old programs. v2 changed the syntax, so v1 scripts do not
run on v2. Rather than freezing the new language to keep them working,
v2 starts this interpreter as a child process when it needs to.

## How it is built

CMake builds it into `red-legacy.jar` when a JDK is present, and skips it
otherwise. Everything else works without Java.

To build it by hand:

```bash
legacy/build.sh
```

That writes `build/red-legacy.jar`. Set `RED_LEGACY_JAR` to point
somewhere else.

## How it is run

From the command line:

```bash
red legacy legacy/main.red
```

From a v2 program:

```red
legacy("legacy/main.red");                  // output goes to the terminal
const text = legacy_output("legacy/main.red");  // captured as a string
print(legacy_available());                  // is the jar built?
```

[`../examples/legacy_bridge.red`](../examples/legacy_bridge.red) shows all
three.

## What is in here

| Path | Contents |
|---|---|
| `redlang/` | The interpreter. Scanner, Parser, Resolver, Interpreter. |
| `tool/GenerateAst.java` | Generates `Expr.java` and `Stmt.java`. |
| `main.red` | The v1 example program. |

`redlang/AstPrinter.java` prints a syntax tree. It was the debugging tool
for v1, in the same way that `red disasm` is for v2.

## What v1 could do

Variables, arithmetic, comparison, `if`, `while`, `for`, functions,
closures, `return`, classes with `init` and methods, `this`, single
inheritance, and one built-in function, `clock()`.

It had no arrays, no maps, no string interpolation, no error handling, no
modules, no concurrency, no standard library and no memory management of
its own. The Java runtime did the last one.
