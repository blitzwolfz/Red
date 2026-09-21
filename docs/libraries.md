# Libraries

A Red library is a file of Red code that other programs import. When part
of it needs to be fast, or needs to reach something the standard library
does not cover, that part can be written in C or C++ and loaded as an
extension. Both halves are described here.

Worked examples live in [`lib/`](../lib):

| Library | Written in |
|---|---|
| [`lib/cli.red`](../lib/cli.red) | Red. Command line parsing. |
| [`lib/crc32.red`](../lib/crc32.red) | Red, with a C++ half in [`ffi/crc32_ext.cpp`](../ffi/crc32_ext.cpp). |

[`examples/library_tour.red`](../examples/library_tour.red) uses both.

## A library written in Red

A library is an ordinary Red file. Everything declared at its top level is
visible to whoever imports it, and nothing else is.

```red
// lib/greet.red
const DEFAULT = "world";

fun hello(who = DEFAULT) {
  return "hello, ${who}";
}

class Greeter {
  init(prefix) { this.prefix = prefix; }
  greet(who) { return "${this.prefix} ${who}"; }
}
```

```red
import "greet.red" as greet;

print(greet.hello());              // hello, world
print(greet.Greeter("hi").greet("ada"));
```

The name after `as` is optional. Without it the binding is the file stem,
so `import "greet.red";` also binds `greet`.

A module's body runs once, the first time it is imported. Later imports
get the same module back. Two modules may import each other: the second
one to start sees the first in its partly built state rather than looping
forever.

## Where the interpreter looks

`import "name.red"` is resolved in this order. The first hit wins.

| Order | Directory |
|---|---|
| 1 | Next to the file doing the importing |
| 2 | Each `:` separated entry of `$RED_PATH`, in order |
| 3 | `lib/red` beside the interpreter's directory |
| 4 | `lib` beside the interpreter's directory |
| 5 | The interpreter's own directory |

Rule 1 is what makes a program's own files work: a relative import is
relative to the importer, not to the directory the program was started
from, so a project can be moved without editing its imports.

Rules 3 to 5 are worked out from where the `red` binary is. Running
`build/red` in this repository, they come to `lib/red`, `lib` and `build`,
which is why `import "cli.red"` finds `lib/cli.red` from anywhere. For an
interpreter installed at `/usr/local/bin/red` they are
`/usr/local/lib/red` and `/usr/local/lib`.

`library_paths()` prints the list rules 2 to 5 produced, which is the
quickest way to find out why an import did not resolve.

```red
for (let directory in library_paths()) { print(directory); }
```

To add a directory of your own:

```bash
export RED_PATH=$HOME/red-libraries:/opt/red/lib
```

A library that needs a file of its own — a data table, or an extension it
ships with — finds it with `source_dir()`, which gives the directory of
the file that called it rather than the working directory.

```red
const words = read_file(source_dir() + "/words.txt");
```

## A library written in C or C++

An extension is a shared library that exports plain C functions. Red loads
it with `ffi_open()` and picks functions out of it with `sym()`.

The contract is in two files, and an extension includes one of them:

| File | For |
|---|---|
| [`ffi/red_ffi.h`](../ffi/red_ffi.h) | C. The contract itself. |
| [`ffi/red_ffi.hpp`](../ffi/red_ffi.hpp) | C++. A header-only layer over it. |

Neither needs anything else from the interpreter's source, and an
extension links against no libraries: the `red_*` helper functions are
resolved inside the `red` binary when the extension is loaded.

### In C++

```cpp
#include <cmath>
#include "red_ffi.hpp"

RED_FUNCTION(mathx_hypot) {
  double a, b;
  if (!args.number(0, &a) || !args.number(1, &b)) {
    return ctx.fail("hypot() expects two numbers");
  }
  return red::ext::number(std::hypot(a, b));
}
```

`RED_FUNCTION` declares the `extern "C"` entry point and hands the body a
`ctx` and an `args`.

| | |
|---|---|
| `args.size()` | How many arguments arrived. |
| `args[i]` | One argument, or nil past the end. |
| `args.number(i, &out)` | False when that argument is missing or is not a number. |
| `args.string(i, &out)` | The same for a string. |
| `red::ext::number(d)`, `boolean(b)`, `nil()` | Values that need no allocation. |
| `ctx.string(text)` | A Red string. Allocates, so it can collect. |
| `ctx.fail(message)` | Raises an error in Red. Return it straight away. |

A `string_view` from `args.string()` points into the interpreter's heap
and stays valid until the next allocation. Copy it if it has to outlive
the call.

### In C

The same thing without the wrapper. `RedValue`, `red_is_number`,
`red_as_number`, `red_number`, `red_new_string`, `red_fail`.

```c
#include "red_ffi.h"

RedValue mathx_double(void* context, int argc, RedValue* argv) {
  if (argc < 1 || !red_is_number(argv[0])) {
    return red_fail(context, "double() expects a number");
  }
  return red_number(red_as_number(argv[0]) * 2);
}
```

[`ffi/example_ext.c`](../ffi/example_ext.c) is a complete one.

### Building it

With CMake, using the helper this repository ships:

```cmake
include(/path/to/red/cmake/RedExtension.cmake)
red_add_extension(mathx mathx.cpp)
```

That produces `mathx.so`, with no `lib` prefix and the same suffix on
every platform, including macOS. Red code can then name the file without
asking what it is running on.

Without CMake:

```bash
# Linux
c++ -std=c++17 -O2 -shared -fPIC -I /path/to/red/ffi mathx.cpp -o mathx.so

# macOS: the red_* helpers are resolved at load time, not at link time
c++ -std=c++17 -O2 -shared -fPIC -undefined dynamic_lookup \
    -I /path/to/red/ffi mathx.cpp -o mathx.so
```

### Loading it

```red
const lib = ffi_open("mathx.so");
const hypot = lib.sym("mathx_hypot");
print(hypot(3, 4));                 // 5
lib.close();
```

A name with no `/` in it is looked for on the same search path as
`import`, so an extension installed beside the interpreter, or in a
directory on `RED_PATH`, is found by bare name. A name with a `/` in it is
used exactly as written.

`ffi_open` raises an error with kind `"ffi"` when the file is missing,
which is what lets a library treat its native half as optional.

## Putting the two together

[`lib/crc32.red`](../lib/crc32.red) is the pattern worth copying. The
library is Red, and it works with no extension installed; when one is
present it is used instead.

```red
let native = nil;
let tried = false;

fun loadNative() {
  if (tried) { return native; }
  tried = true;
  try {
    const lib = ffi_open("crc32_ext.so");
    if (lib.sym("crc32_version")() != "1") { return nil; }
    native = lib.sym("crc32_of");
  } catch (e: "ffi") {
    native = nil;
  }
  return native;
}

fun of(text) {
  const fast = loadNative();
  if (fast != nil) { return fast(text); }
  return pureRedVersion(text);
}
```

Three things make it hold up.

**The version check.** An extension built against an older version of the
library is refused rather than used, so the two halves cannot drift apart
silently.

**The load is lazy and happens once.** Building the table and opening the
shared library both cost something, and a program that never asks for a
checksum pays neither.

**Both halves are tested.** `crc32.use("red")` pins the Red half, so
[`tests/libraries.red`](../tests/libraries.red) can check that the two
agree on every byte value rather than testing whichever one happened to
load.

## Shipping a library

There is no package manager. A library is a file, or a directory of files,
and it is installed by putting it somewhere on the search path.

```
mylib/
  mylib.red          the library
  mylib_ext.cpp      the native half, if it has one
  README.md
  tests/mylib.red    a conformance test, in the shape tests/run.py expects
```

```bash
cp mylib/mylib.red   /usr/local/lib/red/
cp build/mylib_ext.so /usr/local/lib/red/
```

or, without installing anything:

```bash
export RED_PATH=$PWD/mylib
```

Compiled libraries work too. `red compile mylib.red` produces `mylib.redc`
and `import "mylib.redc"` loads it without running the compiler, which is
worth doing for a large library that many programs import.
