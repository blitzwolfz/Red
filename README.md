# Red

A small programming language with a bytecode compiler, a stack virtual
machine, a mark and sweep garbage collector, and tasks with channels.
Written in C++20, with no third-party dependencies.

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

## Build

Needs CMake 3.16 or newer and a compiler with C++20. A JDK is optional,
and only used to build the v1 interpreter.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Then:

```bash
./build/red examples/tour.red
./build/red bench
./build/red repl
```

Compile ahead of time, then run without the compiler:

```bash
./build/red compile examples/tour.red
./build/red examples/tour.redc
```

On a four thousand function program that is 41ms of start-up down to
4.4ms. The compiled file is versioned and checked on load, and is
smaller than the source for ordinary code.

Run the test suite:

```bash
python3 tests/run.py --red build/red --tests tests
python3 tests/run.py --red build/red --tests tests --gc-stress
python3 tests/run.py --red build/red --tests tests --compiled
```

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
| [`src/vm.cpp`](src/vm.cpp) | The dispatch loop, calls, closures, unwinding. |
| [`src/runtime.cpp`](src/runtime.cpp) | Allocation and the collector. |
| [`src/value.h`](src/value.h), [`src/object.h`](src/object.h) | Value layout and heap types. |
| [`src/table.cpp`](src/table.cpp) | Hash tables for globals, fields and maps. |
| [`src/debug.cpp`](src/debug.cpp) | The disassembler, shared with `--trace`. |
| [`src/stdlib/`](src/stdlib) | Built-in functions and methods. |
| [`legacy/`](legacy) | The v1 interpreter, in Java, still working. |

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

## Extensions

Red loads shared libraries and calls into them.

```red
const lib = ffi_open("./example_ext.so");
const hypot = lib.sym("ext_hypot");
print(hypot(3, 4));      // 5
```

[`ffi/red_ffi.h`](ffi/red_ffi.h) is the contract.
[`ffi/example_ext.c`](ffi/example_ext.c) is a working extension.

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
| [`examples/echo_server.red`](examples/echo_server.red) | A concurrent TCP echo server, with clients. |
| [`examples/word_count.red`](examples/word_count.red) | Parallel word count over a file. |
| [`examples/legacy_bridge.red`](examples/legacy_bridge.red) | Calling v1 from v2. |
| [`examples/mini_compiler.red`](examples/mini_compiler.red) | A compiler and virtual machine for arithmetic, written in Red. |

## Documentation

| File | Contents |
|---|---|
| [docs/design.md](docs/design.md) | Why it is built this way, and what was rejected. |
| [docs/language.md](docs/language.md) | Language reference and grammar. |
| [docs/bytecode.md](docs/bytecode.md) | The instruction set. |
| [docs/stdlib.md](docs/stdlib.md) | Built-in functions and methods. |
| [docs/bootstrapping.md](docs/bootstrapping.md) | Plan for removing the C++ dependency. |

## Known limits

- Two tasks do not compute at the same time. One lock guards the heap.
  [Why](docs/design.md#concurrency).
- The collector stops the world and does not move objects.
- Type annotations are parsed and ignored.
- There is no compiled file format yet, so every run compiles from
  source. That is the next milestone, and the last thing between here and
  a self-hosted compiler. See [docs/bootstrapping.md](docs/bootstrapping.md).
- Code that makes many distinct strings is slow, because every string is
  interned. Reusing a small vocabulary is fast.
- A task that is never joined is kept alive until the program ends.

## Licence

See [LICENSE.md](LICENSE.md).
