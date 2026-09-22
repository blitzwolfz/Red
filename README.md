<div align="center">
  <img src="assets/red-panda-logo.png" alt="Red programming language red panda mascot" width="480">

  <p>A small, fast programming language with a bytecode compiler, a stack virtual machine, and lightweight concurrent tasks.</p>

  <p>
    <a href="docs/guide.md">Learn Red</a> ·
    <a href="examples/tour.red">See an example</a> ·
    <a href="docs/language.md">Read the reference</a>
  </p>
</div>

Red is a compact programming language for writing command-line tools,
experiments, and small services. It is written in C++20, has no third-party
runtime dependencies, and includes a compiler, debugger, formatter, standard
library, language server, and test runner.

The compiler is also written in Red. It can compile itself and produces the
same bytecode as the C++ compiler.

## Try it in a minute

### Requirements

- CMake 3.16 or newer
- A C++20 compiler
- Python 3 is optional, for comparing benchmarks
- A JDK is optional, for the legacy v1 interpreter

Red currently targets POSIX systems: macOS and Linux. Windows is not supported.

### Build and test

From the repository root:

```bash
./setup.sh
```

The setup script checks your tools, builds the interpreter at `build/red`, and
runs the test suite. To build without running tests:

```bash
./setup.sh --quick
```

To build with AddressSanitizer and UndefinedBehaviorSanitizer enabled:

```bash
./setup.sh --debug
```

### Run your first program

Create `hello.red`:

```red
const names = args();

if (names.len() == 0) {
  print("hello, world");
} else {
  for (let name in names) { print("hello, ${name}"); }
}
```

Run it with the freshly built interpreter:

```bash
./build/red hello.red
./build/red hello.red Ada Grace
```

Or open the interactive prompt:

```bash
./build/red repl
```

For a guided project, work through [the log summariser tutorial](docs/guide.md)
or run the language tour:

```bash
./build/red examples/tour.red
```

## What Red gives you

Red keeps the syntax small while still covering the features useful for real
programs:

| Feature | Included |
|---|---|
| Execution | Bytecode compiler and stack-based virtual machine |
| Memory | Mark-and-sweep garbage collector |
| Concurrency | Tasks and buffered channels, with safe shared containers |
| Types | Optional annotations, runtime checks, and types as values |
| Data | Arrays, maps, sets, bytes, strings, enums, classes, and modules |
| Errors | Typed errors, filters, `try`/`catch`/`finally`, and payloads |
| Tools | REPL, debugger, formatter, disassembler, test runner, and LSP |
| Native code | C and C++ extensions through a small FFI |
| Distribution | Compile ahead of time or build a standalone executable |

Here is a small concurrent program:

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

The full syntax and semantics are in [the language reference](docs/language.md).

## Everyday commands

The executable built by `setup.sh` is `build/red`:

| Command | Use it to |
|---|---|
| `red program.red [args]` | Run a Red program |
| `red repl` | Try expressions interactively |
| `red compile in.red [-o out]` | Write a `.redc` bytecode file |
| `red build in.red [-o name]` | Create a standalone executable |
| `red test [directory]` | Run Red tests |
| `red debug program.red` | Step through a program and inspect locals |
| `red fmt -w files` | Format source files in place |
| `red fmt --check files` | Check formatting without changing files |
| `red disasm program.red` | Inspect compiled bytecode |
| `red bench` | Run the included benchmarks |
| `red --trace program.red` | Print instructions and the stack while running |

Useful examples:

```bash
# Compile once, then run the bytecode.
./build/red compile examples/tour.red
./build/red examples/tour.redc

# Build a self-contained executable.
./build/red build examples/logstat/logstat.red -o logstat
./logstat examples/logstat/sample.log

# Use the debugger.
./build/red debug examples/word_count.red

# Make the garbage collector collect before every allocation.
./build/red test tests --gc-stress
```

The debugger uses source line information, so give it a `.red` file rather
than a compiled `.redc` file. The formatter checks that its output tokenizes
the same way before it overwrites a file.

## Tests

Tests are ordinary Red programs with expected output written in comments. Run
the complete suite with:

```bash
./build/red test tests
```

The repository also supports these useful variants:

```bash
./build/red test tests --gc-stress   # stress the collector
./build/red test tests --compiled    # run tests from .redc files
./build/red test tests --compiler selfhost/redc.red
```

The last command runs the suite using the self-hosted compiler instead of the
C++ compiler.

## Libraries and native extensions

A library is simply a Red file that another program imports:

```red
import "cli.red" as cli;
```

Imports look beside the importing file first, then on `RED_PATH`, then in the
directories that ship with the interpreter. The repository includes Red-only
libraries such as [`lib/json.red`](lib/json.red) and [`lib/cli.red`](lib/cli.red).

When a library needs native code, write a C or C++ extension and load it with
the FFI:

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
print(lib.sym("mathx_hypot")(3, 4));
```

See [docs/libraries.md](docs/libraries.md) for the search path, extension
build helper, and complete FFI contract.

## A user interface

[`lib/andy`](lib/andy) is a user interface library. The same program draws in
the terminal it was started in, or in a window of its own.

```red
import "andy" as andy;

const name = andy.Input("", "your name");

const form = andy.Column(
  andy.Label("What should I call you?"),
  name,
  andy.Row(andy.Spacer(), andy.Button("Done", fun (b) { andy.stop(); }))
).spaced(1).padded(andy.uniform(1));

andy.run(andy.Center(andy.Panel("Hello", form).sized(44, 9)),
  {"title": "Hello"});

print("hello, ${name.get_value()}");
```

That is [`examples/andy_hello.red`](examples/andy_hello.red). For a tour of
the rest:

```bash
./build/red examples/andy_demo.red                       # in this terminal
ANDY_BACKEND=native ./build/red examples/andy_demo.red   # in a window
```

Rows and columns, panels, grids and scrolling views; labels, buttons,
checkboxes, sliders, text fields, dropdowns, lists, tables, trees, tabs,
menus and dialogs. Widgets name their colours in a theme rather than choosing
them, so a program can be recoloured by somebody who has never read it.

See [docs/andy.md](docs/andy.md).

## A self-hosted compiler

[`selfhost/redc.red`](selfhost/redc.red) contains a compiler written in Red.
The bootstrap check builds that compiler, uses it to compile itself, and
compares the resulting bytecode:

```bash
selfhost/bootstrap.sh
```

The compiler emits the same `.redc` format as the C++ compiler. See
[docs/bootstrapping.md](docs/bootstrapping.md) for the three-stage process.

## Find your way around

| Path | Purpose |
|---|---|
| [`examples/`](examples) | Small runnable programs |
| [`examples/logstat/`](examples/logstat) | A complete multi-file application with tests |
| [`docs/guide.md`](docs/guide.md) | Build a real program from start to finish |
| [`docs/language.md`](docs/language.md) | Language reference and grammar |
| [`docs/stdlib.md`](docs/stdlib.md) | Built-in functions and methods |
| [`docs/libraries.md`](docs/libraries.md) | Red libraries and C/C++ extensions |
| [`docs/andy.md`](docs/andy.md) | The user interface library |
| [`docs/design.md`](docs/design.md) | Runtime and implementation decisions |
| [`docs/bytecode.md`](docs/bytecode.md) | Instructions and `.redc` format |
| [`docs/bootstrapping.md`](docs/bootstrapping.md) | Self-hosting details |
| [`docs/native.md`](docs/native.md) | Native execution notes |
| [`tools/red-lsp.red`](tools/red-lsp.red) | Language server |
| [`editors/README.md`](editors/README.md) | Editor setup |
| [`CHANGELOG.md`](CHANGELOG.md) | Release and compatibility notes |

## Project layout

```text
src/       C++ compiler, runtime, VM, debugger, and standard library
lib/       Libraries shipped with Red
examples/  Programs and a complete logstat example
tests/     Conformance tests
ffi/       C and C++ extension examples
selfhost/  The compiler written in Red
tools/     The language server and development tools
legacy/    The original v1 Java interpreter
docs/      Guides and implementation notes
assets/    Source-controlled project artwork
```

## Current boundaries

Red is intentionally small and still evolving. The important limitations are:

- It supports macOS and Linux, but not Windows.
- Collection is stop-the-world and objects are not moved.
- The type checker is one pass deep; values are checked again when they arrive.
- Regular expressions omit backreferences and lookaround so matching cannot
  take exponentially long.
- The language server does not yet understand types or follow names across
  files.
- The virtual machine, collector, and standard library are still in C++.
- There is no package manager; libraries are installed by copying them onto
  the search path.

## The logo

The red panda mascot is a high-resolution PNG with a transparent outer
background. It is suitable for README art, documentation, app icons, and
other uses where the logo needs to scale down cleanly:

- [`assets/red-panda-logo.png`](assets/red-panda-logo.png) is the 1254 × 1254 source asset.
- [`assets/red-panda-logo-512.png`](assets/red-panda-logo-512.png) is the large app/icon export.
- [`assets/red-panda-logo-128.png`](assets/red-panda-logo-128.png) is the small app/icon export.
- [`assets/red-panda-logo-64.png`](assets/red-panda-logo-64.png) is the favicon-sized export.

## License

See [LICENSE.md](LICENSE.md).
