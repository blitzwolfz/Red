# Red

A small programming language with a bytecode compiler, a stack virtual
machine, a mark and sweep garbage collector, and tasks with channels.
Written in C++20, with no third-party dependencies.

The compiler is also written in Red, and it reproduces itself.

```red
fun worker(jobs, results) {
  for (;;) {
    const job = jobs.recv();
    if (job == nil) { break; }
    results.send("job ${job} done");
  }
}

const jobs = chan(4);
const results = chan(16);
const task = spawn worker(jobs, results);

for (let i = 1; i <= 3; i = i + 1) { jobs.send(i); }
jobs.close();
task.join();
```

## Start here

```bash
./setup.sh
```

That checks what is installed, builds the interpreter into `build/red`,
and runs the test suite. It needs CMake 3.16 or newer and a compiler with
C++20; Python 3 runs the tests, and a JDK is optional and used only for
the v1 interpreter. `./setup.sh --help` lists the rest, including
`--install PREFIX`.

Then:

```bash
./build/red examples/tour.red       # every part of the language
./build/red repl                    # an interactive prompt
./build/red bench                   # the benchmark programs
```

| If you want to | Read |
|---|---|
| Write a program | [docs/guide.md](docs/guide.md), which builds one from nothing |
| Look something up | [docs/language.md](docs/language.md), [docs/stdlib.md](docs/stdlib.md) |
| Write a library, in Red or C++ | [docs/libraries.md](docs/libraries.md) |
| Understand the implementation | [docs/design.md](docs/design.md) |

## The language

Full reference: [docs/language.md](docs/language.md).

```red
// let and const, with optional type hints that nothing checks
let count = 0;
const limit: Int = 10;

// string interpolation and compound assignment
count += 3;
print("${count} of ${limit}");

// arrays and maps, walked with for-in
const names = ["ann", "bob"];
const ages = {"ann": 31, "bob": 25};
for (let name in names) { print(name, ages[name]); }
print(names.map(fun (n) { return ages[n]; }));

// enums, with names that survive into error messages
enum Status { Ok = 200, NotFound = 404 }
print(Status.from(404));          // Status.NotFound

// destructuring, including in a loop
const [first, ...others] = names;
for (let [who, years] in ages.entries()) { print(who, years); }

// switch, with no fall through
switch (count) {
  case 1, 2: print("few");
  case 3: print("three");
  default: print("many");
}

// errors carry a kind, catch clauses select on it, finally always runs
const handle = open(path, "r");
try {
  loadConfig(handle);
} catch (e: ConfigError) {
  report(e.message);
} catch (e: "io") {
  report("could not read the file");
} finally {
  handle.close();
}

// sets, with the usual combining operations
const seen = set(["a", "b"]);
print(seen.union(set(["c"])).len());

// default and rest parameters
fun join(separator = ", ", ...parts) { return parts.join(separator); }

// bitwise operators on 32 bit integers, and byte level access
print((212 << 8) | 49, chr(82), "R".code_at(0));

// classes and single inheritance
class Animal {
  init(kind) { this.kind = kind; }
  speak() { return "..."; }
}
class Dog < Animal {
  init() { super.init("dog"); }
  speak() { return "woof"; }
}

// errors are values
try {
  throw error("bad input", {"field": "age"});
} catch (e) {
  print(e.message, e.payload["field"]);
}

// text is characters, data is bytes
print("h\u00e9llo".char_len(), "h\u00e9llo".len());   // 5 6

// regular expressions, with no way to make one take exponential time
const stamp = regex("(\\d{4})-(\\d{2})-(\\d{2})");
print(stamp.find("due 2024-02-29")["groups"]);    // ["2024", "02", "29"]

// running another program, with the arguments passed through untouched
print(run(["git", "rev-parse", "HEAD"])["out"].trim());

// modules
import "util.red" as util;

// tasks
const task = spawn expensive(input);
print(task.join());
```

## Tools

| Command | What it does |
|---|---|
| `red program [args]` | Runs a program, source or compiled. |
| `red compile in.red [-o out]` | Compiles ahead of time to a `.redc` file. |
| `red test [directory]` | Runs the tests in a directory. |
| `red repl` | Interactive prompt. Handles multi-line input. |
| `red disasm script.red` | Prints the compiled bytecode. |
| `red bench` | Runs the benchmark programs. |
| `red legacy script.red` | Runs a script on the v1 interpreter. |
| `red --trace script.red` | Prints every instruction and the stack. |
| `red --gc-log script.red` | Reports each collection. |
| `red --gc-stress script.red` | Collects before every allocation. |

`--gc-stress` is the one that matters. It turns a rare collector bug into
one that happens on the first run. It found two real bugs while this was
being written.

```
$ red disasm examples/tiny.red
== square ==
0000    1 GET_LOCAL             1
0002    | GET_LOCAL             1
0004    | MULTIPLY
0005    | RETURN
```

Compiling ahead of time removes start-up, not run time:

```bash
red compile examples/tour.red
red examples/tour.redc
```

On a four thousand function program that is 41ms of start-up down to
4.4ms. The compiled file is versioned and checked on load, and is smaller
than the source for ordinary code.

Run the test suite. A test is a Red program with its expected output
written in it as comments, and `red test` runs a directory of them:

```bash
./build/red test tests
./build/red test tests --gc-stress
./build/red test tests --compiled
./build/red test tests --compiler selfhost/redc.red
```

`--gc-stress` collects before every allocation, `--compiled` runs each
test from a `.redc`, and `--compiler` puts the Red compiler under the
whole suite instead of the C++ one.

## Libraries

A library is a file of Red that other programs import. `import` looks
next to the importing file, then on `RED_PATH`, then in the directories
that ship with the interpreter, so a library installed once is reachable
by bare name.

```red
import "cli.red" as cli;
```

When part of a library needs to be fast, or needs something the standard
library does not cover, that part can be written in C or C++ and loaded
as an extension.

```cpp
#include "red_ffi.hpp"

RED_FUNCTION(mathx_hypot) {
  double a, b;
  if (!args.number(0, &a) || !args.number(1, &b)) {
    return ctx.fail("hypot() expects two numbers");
  }
  return red::ext::number(std::hypot(a, b));
}
```

```red
const lib = ffi_open("mathx.so");
print(lib.sym("mathx_hypot")(3, 4));      // 5
```

[`lib/cli.red`](lib/cli.red) and [`lib/json.red`](lib/json.red) are
written entirely in Red. [`lib/crc32.red`](lib/crc32.red) has both
halves: Red that works on its own, and a C++ extension it uses when one
is installed. [docs/libraries.md](docs/libraries.md) covers writing
either.

## The compiler, in Red

[`selfhost/redc.red`](selfhost/redc.red) is a compiler for Red, written
in Red. It emits the same bytecode and writes the same `.redc` files as
the C++ compiler.

```bash
$ selfhost/bootstrap.sh
B and C are identical. The compiler reproduces itself.
A and B are identical too: the two compilers agree byte for byte.
50 identical, 0 different
31/31 tests passed (compiled by the self-hosted compiler)
```

The first line is the classic test: compile the Red compiler with the C++
one, then with itself twice, and the last two results must match. The
second is stronger — the two compilers produce the same bytes for every
Red program in the repository. The third compiles each of those with both
and compares; the fourth runs the whole conformance suite on bytecode the
Red compiler produced.

It compiles itself, 2,800 lines, in about 80ms. The C++ compiler does the
same file in 3.6ms, so the Red one is roughly twenty times slower, which
is about what an interpreted compiler costs and fast enough that the
three stage bootstrap finishes in under a second.
[docs/bootstrapping.md](docs/bootstrapping.md) has the plan this
finished, and what removing the rest of the C++ would mean.

## Numbers

Measured on an Apple M3, against CPython 3.14. Each program is run three
times and the fastest run is reported. Both versions of each benchmark do
the same work and their output is compared. Reproduce with
`python3 bench/compare.py --red build/red --markdown`.

| benchmark | what it measures | red | python | ratio |
|---|---|--:|--:|--:|
| fib | recursive calls, no allocation | 0.20s | 0.16s | 1.24x |
| loop | tight arithmetic loop | 1.06s | 0.90s | 1.18x |
| string | building and inspecting short strings | 0.64s | 0.11s | 5.61x |
| alloc | allocation churn, collector bound | 0.43s | 0.29s | 1.45x |
| method | method dispatch through inheritance | 0.43s | 0.51s | 0.85x |

A ratio below 1.00 means Red was faster.

Red is in the same range as CPython on calls, loops and allocation, and
faster on method dispatch. It is about five times slower on string work,
because every string is interned. That is a known cost of the current
design and it is explained in [docs/design.md](docs/design.md#value-layout).

## Architecture

```
  source                                       ┌──────────────────┐
    │                                          │     Runtime      │
    ▼                                          │                  │
┌─────────┐   tokens   ┌──────────┐            │  heap            │
│ Scanner │ ─────────► │ Compiler │            │  string interner │
└─────────┘            └──────────┘            │  module cache    │
                            │                  │  collector       │
                            │ bytecode         │  one lock        │
                            ▼                  └──────────────────┘
                       ┌──────────┐                 ▲    ▲    ▲
                       │  Chunk   │                 │    │    │
                       │ code     │            ┌────┘    │    └────┐
                       │ constants│            │         │         │
                       │ lines    │        ┌───┴──┐  ┌───┴──┐  ┌───┴──┐
                       └──────────┘        │  VM  │  │  VM  │  │  VM  │
                            │              │ task │  │ task │  │ task │
                            └─────────────►│      │  │      │  │      │
                                           │stack │  │stack │  │stack │
                                           │frames│  │frames│  │frames│
                                           └──────┘  └──────┘  └──────┘
```

There is no syntax tree. The compiler reads one token at a time and emits
bytecode directly.

One `Runtime` per process holds the heap. One `VM` per task holds a value
stack and call frames. A task holds the runtime lock while it runs
bytecode, and releases it before anything that waits. The collector runs
only while that lock is held, so any task that is not holding it has a
stack that is not moving and can be scanned safely.

| Where | What |
|---|---|
| [`src/scanner.cpp`](src/scanner.cpp) | Tokens, including string interpolation. |
| [`src/compiler.cpp`](src/compiler.cpp) | Single pass, Pratt expressions, emits bytecode. |
| [`src/serialize.cpp`](src/serialize.cpp) | Reading and writing `.redc` files. |
| [`src/vm.cpp`](src/vm.cpp) | The dispatch loop, calls, closures, unwinding. |
| [`src/runtime.cpp`](src/runtime.cpp) | Allocation and the collector. |
| [`src/value.h`](src/value.h), [`src/object.h`](src/object.h) | Value layout and heap types. |
| [`src/table.cpp`](src/table.cpp) | Hash tables for globals, fields and maps. |
| [`src/debug.cpp`](src/debug.cpp) | The disassembler, shared with `--trace`. |
| [`src/stdlib/`](src/stdlib) | Built-in functions and methods. |
| [`selfhost/redc.red`](selfhost/redc.red) | The same scanner, compiler and writer, in Red. |
| [`lib/`](lib) | Libraries that ship with the interpreter. |
| [`ffi/`](ffi) | The extension contract, in C and C++. |
| [`legacy/`](legacy) | The v1 interpreter, in Java, still working. |

## Running v1 code

The original Red was a tree-walking interpreter in Java. It is still in
[`legacy/`](legacy), it still builds, and v2 still runs it by starting it
as a child process.

```bash
red legacy legacy/main.red
```

```red
const output = legacy_output("legacy/main.red");
print(output.split("\n").len());
```

## Examples

| File | What it shows |
|---|---|
| [`examples/tour.red`](examples/tour.red) | Every part of the language. |
| [`examples/logstat/`](examples/logstat) | A whole program: arguments, modules, files, tasks, tests. |
| [`examples/library_tour.red`](examples/library_tour.red) | Using libraries, in Red and in C++. |
| [`examples/echo_server.red`](examples/echo_server.red) | A concurrent TCP echo server, with clients. |
| [`examples/word_count.red`](examples/word_count.red) | Parallel word count over a file. |
| [`examples/legacy_bridge.red`](examples/legacy_bridge.red) | Calling v1 from v2. |
| [`examples/mini_compiler.red`](examples/mini_compiler.red) | A compiler and virtual machine for arithmetic, written in Red. |

## Documentation

| File | Contents |
|---|---|
| [docs/guide.md](docs/guide.md) | Writing a whole program, start to finish. |
| [docs/language.md](docs/language.md) | Language reference and grammar. |
| [docs/stdlib.md](docs/stdlib.md) | Built-in functions and methods. |
| [docs/libraries.md](docs/libraries.md) | Writing a library, in Red or in C++. |
| [docs/design.md](docs/design.md) | Why it is built this way, and what was rejected. |
| [docs/bytecode.md](docs/bytecode.md) | The instruction set and the compiled file format. |
| [docs/bootstrapping.md](docs/bootstrapping.md) | How the C++ dependency is being removed. |
| [docs/native.md](docs/native.md) | Whether to leave the virtual machine, and why not yet. |
| [CHANGELOG.md](CHANGELOG.md) | What changed, and what it breaks. |
| [selfhost/README.md](selfhost/README.md) | The compiler written in Red, and how to bootstrap it. |

## Known limits

- Two tasks do not compute at the same time. One lock guards the heap.
  What tasks buy is waiting in parallel, not computing in parallel.
  [Why](docs/design.md#concurrency).
- The collector stops the world and does not move objects.
- Type annotations are parsed and ignored.
- Code that makes many distinct strings is slow, because every string is
  interned. Reusing a small vocabulary is fast.
- A task that is never joined is kept alive until the program ends.
- An instance is a map key by identity, not by value. A class can define
  `eq()` for `==`, but not how it hashes, because the table cannot call
  back into Red while it is probing.
  [Why](docs/language.md#str-and-eq).
- `upper()`, `lower()` and the regex `i` flag only change ASCII letters.
- Regular expressions have no backreferences and no lookaround, which is
  the price of never taking exponentially long.
  [Why](docs/stdlib.md#regular-expressions).
- POSIX only. It builds on macOS and Linux; there is no Windows port.
- No language server, no formatter, no debugger beyond `--trace`.
  Syntax highlighting is in [editors/](editors).
- The virtual machine, the collector and the standard library are still
  C++. Only the compiler is self-hosted.
- There is no package manager. A library is installed by copying it onto
  the search path.

## Licence

See [LICENSE.md](LICENSE.md).
