# Changes

Red follows no release schedule. The bytecode format has a version of its
own, tracked in [docs/bytecode.md](docs/bytecode.md), and a `.redc` built
by one version is refused rather than misread by another.

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
