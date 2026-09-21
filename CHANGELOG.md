# Changes

Red follows no release schedule. The bytecode format has a version of its
own, tracked in [docs/bytecode.md](docs/bytecode.md), and a `.redc` built
by one version is refused rather than misread by another.

## 0.4.0

Bytecode format: **3**, unchanged. Every `.redc` built by 0.2.0 still
runs, and extensions built against 0.3.0 still load.

### Added

- `red debug program.red`. Stops on lines; `s` steps into a call, `n`
  over it, `f` out of it, `b` sets a breakpoint, `v` lists the locals in
  the frame by name, `p` prints one, `bt` shows the stack. Needs the
  source: a `.redc` has no names in it.
- `red fmt`. Re-indents and re-spaces, does not re-wrap. `-w` rewrites in
  place, `--check` reports what would change. It lexes its own output and
  refuses if the tokens differ, so a bug in it cannot mangle a file.
  Every `.red` file here is formatted with it and CI checks that.
- A language server, [`tools/red-lsp.red`](tools/red-lsp.red), written in
  Red: diagnostics, document symbols, go to definition, hover,
  completion. [editors/README.md](editors/README.md) has the settings.
- `read(count)` on a file handle, and `exe_path()`.

### Changed

- **`upper()` and `lower()` cover all of Unicode**, including the
  mappings that change length: `"straße".upper()` is `"STRASSE"`. The
  regex `i` flag uses the same tables. Code that relied on non-ASCII
  characters passing through unchanged will see them converted.
- **Only strings of one or two characters are interned.** Longer ones
  compare by pointer, then hash, then contents. Building a string went
  from 297ns to 230ns and the `string` benchmark from 5.6x CPython to
  3.9x. Nothing visible changed; `==` still compares strings by value.
- The dispatch loop is compiled twice, once with the debugger and tracer
  and once without them at all. Every benchmark is 7 to 12% faster than
  0.3.0 as a result.
- String constants in a `.redc` are interned on load. Without that, a
  field name longer than two characters defined in one module and used
  from another would not have been found.

### Tried and rejected

Both are written up with their numbers, in
[docs/design.md](docs/design.md#value-layout) and
[docs/native.md](docs/native.md).

- **NaN boxing.** Calls and arithmetic got 5 to 12% worse: a boxed double
  arrives in an integer register and every operation has to move it to a
  floating point one and back. `Value` stays sixteen bytes, and the FFI
  keeps its layout.
- **An inline method cache.** Measured with a single global entry that no
  per-site cache could beat. No difference at all: method lookup is not
  where a call's time goes.

## 0.3.0

Bytecode format: **3**, unchanged. Every `.redc` built by 0.2.0 still
runs.

### Added

- Regular expressions. `regex(pattern, flags)` with `test`, `find`,
  `find_all`, `replace`, `split`. The matcher follows every alternative
  at once, so there is no pattern that takes exponentially long.
  Backreferences and lookaround are not supported, which is the trade.
- Running other programs: `run(argv, input)`, `shell(command, input)` and
  `which(name)`. `run` passes the arguments through untouched, so a value
  from outside the program cannot turn into another command.
- Text as distinct from bytes: `chars()`, `code_points()`, `char_len()`
  and `char(code)`, and `for ... in` over a string now walks characters.
  `\u` escapes in string literals.
- Hex number literals, `0xff`.
- `str()` and `eq()` on a class, which decide how its instances print and
  compare. Instances may be map keys and set members, by identity.
- `red test <directory>` runs a directory of tests. `--gc-stress`,
  `--compiled`, `--filter` and `--compiler`.
- `eprint` and `ewrite`, for the error stream.
- `rand`, `rand_seed`, `round`, `sign`, `exp`, `log`, `hypot`, the
  trigonometric functions, `PI` and `E`.
- `list_dir`, `mkdir`, `remove_dir`, `rename`, `is_dir`, `is_file`,
  `file_size`, `modified`.
- `date()` and `format_time()`.
- `any`, `all`, `find` and `find_index` on arrays; `clear` on maps;
  `trim_start` and `trim_end` on strings.
- `source_path()`, `source_dir()` and `library_paths()`.
- A JSON library, [`lib/json.red`](lib/json.red), written in Red.
- Syntax highlighting for Vim and for TextMate based editors, in
  [`editors/`](editors).
- [docs/native.md](docs/native.md), on whether to leave the virtual
  machine, and why the answer is currently no.

### Changed

- **`str()` of a number now round trips.** It writes the shortest text
  that reads back as the same double, where it used to use a fixed
  precision that silently dropped the last few bits. `str(0.1 + 0.2)` was
  `0.3` and is now `0.30000000000000004`. Whole numbers print in full up
  to 2^53 rather than up to 1e15, so `1e15` prints as
  `1000000000000000` and `1e16` as `1e+16`.
- **An error raised inside a task comes out of `join()` as itself**, with
  its kind and payload, where it used to arrive as kind `"task"` with the
  message wrapped in a sentence. Code that catches `"task"` around a join
  should catch the kind the task actually raises.
- `for ... in` over a string yields characters rather than bytes, and
  `set(text)` holds characters. `bytes()` is unchanged.
- An instance is accepted as a map key, so the message naming what a key
  may be has changed.
- `tests/run.py` is gone; `red test` replaces it, and `ctest` calls the
  interpreter directly.

### Fixed

- The runtime lock is released while another program runs, so `run()` and
  `shell()` do not stop the other tasks.

## 0.2.0

The rewrite. A bytecode compiler, a stack virtual machine, a mark and
sweep collector and tasks with channels, in C++20, replacing the v1
tree-walking interpreter in Java, which is still in
[`legacy/`](legacy) and still runs.

Along the way: ahead-of-time compilation to `.redc`, constant folding,
enums, destructuring, `switch`, `finally`, filtered `catch` clauses,
sets, default and rest parameters, bitwise operators, an FFI, a library
search path, and a compiler written in Red that reproduces itself.

## 0.1.0

The v1 interpreter, in Java.
