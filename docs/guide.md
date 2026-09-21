# Writing a program in Red

This builds one program from nothing to something you could install: a
log summariser called `logstat`. It reads access logs, counts what it
finds, and prints a table.

```
$ red logstat.red access.log
lines     9
skipped   1
bytes     17511

status  requests
200     6
401     1
404     1
500     1

path         requests
/index.html  3
/api/items   2
/api/login   2
/missing     1
/style.css   1
```

The finished program is in [`examples/logstat/`](../examples/logstat),
and every listing below is a real part of it. It is a small program, but
it is a whole one: arguments, modules, files, errors, tasks, output, exit
codes and tests. The language reference tells you what `switch` does.
This is about how the pieces sit together.

Work through it in order, or read [the finished
program](../examples/logstat/logstat.red) first and come back for the
parts that look odd.

- [Before you start](#before-you-start)
- [1. A program that runs](#1-a-program-that-runs)
- [2. Laying out a project](#2-laying-out-a-project)
- [3. Where the decisions live](#3-where-the-decisions-live)
- [4. Tests](#4-tests)
- [5. Reading files, and failing well](#5-reading-files-and-failing-well)
- [6. The command line](#6-the-command-line)
- [7. Printing](#7-printing)
- [8. Doing it in parallel](#8-doing-it-in-parallel)
- [9. Making it faster](#9-making-it-faster)
- [10. Shipping it](#10-shipping-it)
- [Where to go next](#where-to-go-next)

## Before you start

```bash
./setup.sh
```

That builds the interpreter into `build/red` and runs the test suite. If
something is missing it says what and how to install it.

Keep two things open while you work.

```bash
./build/red repl
```

The prompt evaluates a bare expression and prints it, and keeps reading
while brackets are open, so a class or a loop can be typed in over
several lines.

```bash
./build/red debug program.red
```

The debugger stops on lines. `s` steps into a call, `n` over it, `f` out
of it, `b 14` sets a breakpoint, `v` lists the locals in the current
frame by name and `bt` shows the call stack. It needs the source rather
than a `.redc`, because a compiled file carries no names.

```bash
./build/red --trace program.red
```

That prints every instruction and the stack as it runs. Below the
debugger. Reach for it when your question is about the bytecode, not
about the program.

## 1. A program that runs

A Red program is a file. There is no main function, no preamble, no
project file. The top level of the file is the program.

```red
// hello.red
print("hello");
```

```bash
$ red hello.red
hello
```

Arguments come from `args()`, which gives everything after the program's
own name.

```red
const names = args();
if (names.len() == 0) {
  print("usage: hello NAME...");
  exit(64);
}
for (let name in names) { print("hello, ${name}"); }
```

```bash
$ red hello.red ada grace
hello, ada
hello, grace
```

Adopt two conventions from the first line you write.

**Put the work in a `main` function and return a status.** Top level code
cannot use `return`, so a program written directly at the top level ends
up with its exit paths scattered through `if` branches. One function with
one return type is easier to follow, and it is how `logstat.red` ends:

```red
const status = main();
if (status != 0) { exit(status); }
```

**Use the conventional exit codes.** Zero for success, 64 for a command
line that made no sense, 74 for input that could not be read. The
interpreter itself uses 65 for a program that did not compile and 70 for
one that failed while running, so staying out of those is useful too.

```red
const EXIT_USAGE = 64;
const EXIT_INPUT = 74;
```

## 2. Laying out a project

`logstat` is four files.

```
logstat/
  logstat.red        the program: arguments in, text out
  parse.red          the module: text in, values out
  sample.log         something to run it on
  tests/parse.red    the test
```

Everything declared at the top level of `parse.red` is visible to whoever
imports it, and nothing else is.

```red
import "parse.red" as parse;

const report = parse.Report();
```

The name after `as` is optional; without it the binding is the file stem.
A relative import resolves against **the file doing the importing**, not
against the directory the program was started from, so the project can be
moved or run from anywhere and the imports keep working.

The split is the only structural decision in the program, and it is worth
making deliberately:

| File | Knows about |
|---|---|
| `parse.red` | Log lines, requests, counting. Nothing about files, arguments or printing. |
| `logstat.red` | Arguments, files, tasks, output. Nothing about log line syntax. |

That is what makes the next section possible. A module that only turns
values into other values can be tested by calling it.

A module's body runs once, the first time it is imported, and later
imports get the same module back. Two modules may import each other: the
second one to start sees the first half-built. Better than looping
forever, which is the other option.

For libraries that are not part of your project — your own, or the ones
that ship with Red — see [libraries.md](libraries.md). `logstat` uses one:

```red
import "cli.red" as cli;
```

That file is not next to `logstat.red`. It is found on the library search
path, and `library_paths()` prints where the interpreter looked.

## 3. Where the decisions live

Start with the values, not with the input or the output.

A log line is text. What the program actually works with is a request:

```red
class Request {
  init(method, path, status, bytes) {
    this.method = method;
    this.path = path;
    this.status = status;
    this.bytes = bytes;
  }
}
```

Fields are set in `init` and used everywhere else. There is no
declaration list and no types to write; `this.path` exists because
something assigned it.

Then the function that produces one:

```red
fun parse(line) {
  const open = line.find("\"");
  if (open < 0) { return nil; }
  const close = line.find("\" ");
  if (close <= open) { return nil; }

  const request = line.sub(open + 1, close).split(" ");
  if (request.len() < 2) { return nil; }

  const tail = line.sub(close + 2).trim().split(" ");
  if (tail.len() < 2) { return nil; }

  const status = num(tail[0]);
  if (status == nil) { return nil; }

  let bytes = 0;
  if (tail[1] != "-") {
    bytes = num(tail[1]);
    if (bytes == nil) { return nil; }
  }

  return Request(request[0], request[1], status, bytes);
}
```

Note what this does **not** do. It does not throw. A log with a mangled
line in it is ordinary, not exceptional, so a line that does not parse
comes back as `nil` and the caller counts it. Section 5 is about the
cases that should throw.

The checks are cheap because of `num()`. A string that is not entirely a
number gives `nil`, not zero, so `num("oops")` and `num("0")` can never
be confused.

Then the thing that accumulates:

```red
class Report {
  init() {
    this.lines = 0;
    this.skipped = 0;
    this.bytes = 0;
    this.byStatus = {};
    this.byPath = {};
  }

  add(line) {
    const request = parse(line);
    if (request == nil) {
      this.skipped += 1;
      return this;
    }
    this.lines += 1;
    this.bytes += request.bytes;
    this.byStatus.set(request.status,
                      this.byStatus.get(request.status, 0) + 1);
    this.byPath.set(request.path, this.byPath.get(request.path, 0) + 1);
    return this;
  }
}
```

`map.get(key, fallback)` with a fallback of `0` is the whole of counting
in Red. There is no separate counter type and none is needed.

One decision here pays for itself later. `Report` has a `merge`:

```red
merge(other) {
  this.lines += other.lines;
  this.skipped += other.skipped;
  this.bytes += other.bytes;
  for (let [status, count] in other.byStatus.entries()) {
    this.byStatus.set(status, this.byStatus.get(status, 0) + count);
  }
  for (let [path, count] in other.byPath.entries()) {
    this.byPath.set(path, this.byPath.get(path, 0) + count);
  }
  return this;
}
```

Two reports add up. That is what will let one task handle each file in
section 8, and it costs nothing to add now.

`entries()` gives each pair as a two element array, and the
`for (let [k, v] in ...)` form destructures it in the loop header, which
is why the body reads like a table. No `pair[0]` and `pair[1]`
anywhere.

Sorting needs one more thought than it looks like:

```red
busiest(limit) {
  const rows = this.byPath.entries();
  rows.sort(fun (a, b) {
    if (a[1] != b[1]) { return a[1] > b[1]; }
    return a[0] < b[0];
  });
  return rows.slice(0, min(limit, rows.len()));
}
```

`sort` takes a function that says whether the first argument comes
earlier. The second line is the important one: a map's keys come back in
no particular order, so without a tie break two runs over the same file
could print the same rows in a different order. Deciding ties by path
makes the output reproducible, which matters as soon as anything compares
two runs.

## 4. Tests

A test is a Red program with its expected output written in it as
comments.

```red
// tests/parse.red
import "../parse.red" as parse;

const LINE =
    "10.0.0.1 - - [10/Oct/2024:13:55:36 +0000] \"GET /index.html HTTP/1.1\" 200 2326";

const request = parse.parse(LINE);
print(request.method);                    // expect: GET
print(request.path);                      // expect: /index.html
print(request.status);                    // expect: 200

print(parse.parse("nonsense") == nil);    // expect: true
```

```bash
$ red test examples/logstat/tests
ok   parse.red

1/1 tests passed
```

Three kinds of expectation are understood:

```red
print(1 + 1);                        // expect: 2
// expect runtime error: Division by zero.
// expect compile error: Expect ';'
```

Lines marked `expect` must appear on standard output in that order, and
an expected error is matched against standard error. A test with a
runtime error must exit 70; one with a compile error must exit 65.

Test the module, not the program. `parse.red` is where the decisions are;
`logstat.red` moves values between it and the terminal, and testing that
means testing `print`.

Three flags matter.

```bash
red test tests --gc-stress
```

Collects before every allocation. It turns a collector bug from something
that happens rarely into something that happens on the first run. If your
program uses an extension, run it this way at least once.

```bash
red test tests --compiled
```

Compiles each test ahead of time and runs the result, which is worth
doing before shipping anything compiled.

```bash
red test tests --filter parse
```

Only the tests whose name contains that text, for when you are working on
one of them.

## 5. Reading files, and failing well

```red
fun readFile(path) {
  const text = read_file(path);
  if (text == nil) {
    throw error("cannot read '${path}'", path, "io");
  }
  const report = Report();
  for (let line in text.split("\n")) {
    if (line.trim() == "") { continue; }
    report.add(line);
  }
  return report;
}
```

This one throws, and the line above did not. The rule that decides which:

> Return `nil` when the caller can reasonably carry on. Throw when
> carrying on would mean making something up.

A mangled log line is ordinary — skip it and count it. A file that is not
there is not: there is no report to return, and quietly returning an
empty one would report zero requests for a log that was never read.

`error(message, payload, kind)` takes three things. The message is for a
person. The payload is for the program — here the path, so a handler can
name the file without taking the message apart. The kind is what `catch`
selects on:

```red
try {
  const report = parse.readFile(path);
} catch (e: "io") {
  print("logstat: " + e.message);
}
```

A filter can also be a class, in which case it matches instances of that
class, which is how you get typed errors without a type system:

```red
class ConfigError {
  init(message) { this.message = message; }
}

try {
  throw ConfigError("port is not a number");
} catch (e: ConfigError) {
  print(e.message);
}
```

`finally` runs on every way out of a `try`, including a `return` from
inside it. That is why it is where you close things:

```red
const handle = open(path, "r");
try {
  return handle.lines();
} finally {
  handle.close();
}
```

## 6. The command line

`args()` gives you an array of strings. Turning that into flags and
options is the same fifty lines in every program, so it is a library.

```red
import "cli.red" as cli;

fun describe() {
  const spec = cli.Spec("logstat", "Summarises access logs.");
  spec.option("top", "t", "5", "how many paths to list");
  spec.flag("json", "j", "write a JSON object instead of a table");
  spec.flag("quiet", "q", "totals only, no path table");
  spec.flag("version", "", "print the version and stop");
  spec.rest("file", "log files to read, or none to read standard input");
  return spec;
}
```

It understands `--top 3`, `--top=3`, `-t 3`, and `--` to stop parsing.
`--help` is always there.

```red
let options = nil;
try {
  options = spec.parse(args());
} catch (e: "usage") {
  print(e.message);
  print("");
  print(spec.usage());
  return EXIT_USAGE;
}

if (options.flag("help")) {
  print(spec.usage());
  return 0;
}
```

A bad command line is the user's mistake, not a crash. Say what was
wrong, show what was expected, exit 64. `spec.usage()` builds the message
from the spec, so it cannot drift from what the program accepts:

```
Usage: logstat [options] [file...]

Summarises access logs.

Options:
  -h, --help         show this message
  -t, --top <value>  how many paths to list (default 5)
  -j, --json         write a JSON object instead of a table
  -q, --quiet        totals only, no path table
      --version      print the version and stop

  file...            log files to read, or none to read standard input
```

Validate values as you read them, once:

```red
const top = options.number("top");
if (top == nil or top < 1) {
  print("--top needs a positive number, got '${options.option("top")}'");
  return EXIT_USAGE;
}
```

`options.number()` is `num()` on the string, so a value that is not a
number comes back `nil`, not zero.

Reading standard input when no files are named costs four lines and makes
the program compose with everything else:

```red
fun readStandardInput() {
  const report = parse.Report();
  for (;;) {
    const line = input();
    if (line == nil) { break; }
    if (line.trim() == "") { continue; }
    report.add(line);
  }
  return report;
}
```

`input()` gives `nil` at the end of input and an empty string for a blank
line. Those are two different things and the difference matters.

## 7. Printing

`print` writes its arguments separated by spaces and adds a newline;
`write` writes them with neither. String interpolation covers most of
what would otherwise be formatting:

```red
print("lines     ${report.lines}");
```

Anything can go in `${}` — it is compiled as `"..." + str(x) + "..."`.

Columns line up with `pad_right` and `pad_left`. Measure the width
first; do not guess it:

```red
let width = 4;
for (let [path, count] in rows) {
  if (path.len() > width) { width = path.len(); }
}
print("path".pad_right(width + 2) + "requests");
for (let [path, count] in rows) {
  print(path.pad_right(width + 2) + str(count));
}
```

Neither pad truncates. Anything already wider than the column comes back
unchanged, so one long path pushes the row out. It never loses its
second half.

If another program is going to read the output, give it a flag of its
own. Do not make the human table machine readable:

```red
if (options.flag("json")) {
  printJson(report, top);
} else {
  printTable(report, top, options.flag("quiet"));
}
```

`print` builds a whole line and writes it in one go, so two tasks cannot
interleave in the middle of one. Anything you want printed atomically has
to be one call.

## 8. Doing it in parallel

`spawn call(...)` starts a task. `task.join()` waits for it and gives
back what it returned.

```red
fun readAll(paths) {
  const tasks = [];
  for (let path in paths) { tasks.push(spawn parse.readFile(path)); }

  const total = parse.Report();
  const failures = [];
  for (let task in tasks) {
    try {
      total.merge(task.join());
    } catch (e: "io") {
      failures.push(e.message);
    }
  }
  return [total, failures];
}
```

Three things about this shape are deliberate.

**Spawn every task first, then join them in order.** The files are read at
the same time, but merged in the order they were named, so the output
does not depend on which task finished first.

**An error comes back out of `join()` as itself**, with the kind and the
payload it was raised with. The `catch (e: "io")` here is the same clause
that would have worked had `readFile` been called directly, and the trace
in the error still points at the line inside the task where it went
wrong.

**One bad file does not lose the others.** The failures are reported and
the rest of the work is still printed. Only the exit code says something
went wrong:

```bash
$ logstat nope.log access.log
logstat: cannot read 'nope.log'
lines     9
...
$ echo $?
74
```

Tasks can compute at the same time. Each task has its own VM stack and
allocation state; shared values are guarded only while an operation reads
or changes them. Reading ten files can overlap both the waits and the
parsing work. If several tasks repeatedly mutate one array or map, that
single value becomes the point of contention, so prefer channels or
per-task results followed by a merge. [design.md](design.md#concurrency)
explains the collector and sharing rules.

Channels are how tasks that are not simply joined talk to each other:

```red
const jobs = chan(4);
const results = chan(16);
const worker = spawn work(jobs, results);

for (let job in queue) { jobs.send(job); }
jobs.close();
worker.join();
```

`recv()` gives `nil` when the channel is closed and empty, which is the
signal for a worker loop to stop.

## 9. Making it faster

Measure before changing anything.

```red
const start = clock();
const report = parse.readFile(path);
print("${clock() - start}s");
```

Use `clock()`. It is processor time. `time()` is the wall clock and moves
for reasons that have nothing to do with your program.

Two facts about Red, before you optimise the wrong thing.

**Every string is interned.** Building many *distinct* strings is slow,
because each one is hashed and looked up. Reusing a small vocabulary is
fast, and so is indexing a string one character at a time, because there
are only so many distinct characters and they are already in the table.
If a loop is slow and it builds strings, that is the first thing to look
at: collect the pieces in an array and `join("")` once at the end rather
than appending in a loop.

**Compiling ahead of time removes start-up, not run time.**

```bash
$ red compile logstat.red
logstat.red -> logstat.redc (3399 bytes)
$ red logstat.redc access.log
```

A `.redc` holds the same bytecode the compiler would have produced, so a
program that runs for a second runs for a second either way. What it
removes is the compiling: on a four thousand function program that is
41ms down to 4.4ms. Do it for anything started often, and for a large
library that many programs import.

Two things to know. A compiled file records its bytecode version, and an
interpreter expecting a different one refuses it. Nothing is ever
misread. So compile as part of your build; do not commit the result. And a relative import in a compiled program resolves against
the `.redc`, so compile in place — `red compile logstat.red` next to
`parse.red` — rather than into a separate directory.

If a genuinely hot loop is left after all that, it can move into an
extension written in C or C++, with the Red version kept as the fallback.
[libraries.md](libraries.md) has the pattern, and
[`lib/crc32.red`](../lib/crc32.red) is a working example of it.

## 10. Shipping it

A Red program is its source. Shipping it means putting the files
somewhere and making sure the imports still resolve.

**As a single executable**, which is the shortest answer for anything
going to someone who does not have Red:

```bash
$ red build logstat.red -o logstat
logstat.red -> logstat (567098 bytes, 8650 of them program, 3 modules)
$ ./logstat access.log
```

`red build` copies the interpreter, appends the compiled program and
every module it imports, and marks the result executable. The file that
comes out needs nothing else on the machine: no Red, no `RED_PATH`, no
library directory. Copy it and run it.

The size is almost all interpreter, so it is close to the same half
megabyte whether the program is five lines or five thousand. An import
inside a built program is answered from inside the file, under the paths
the build recorded, so the sources can be deleted afterwards and it
still runs.

Two things it does not carry. A native extension is a `.so` that the
operating system has to load from a real path, so a program using one
still needs that file beside it. And a built program is for the platform
that built it: build on Linux for Linux, on macOS for macOS.

**For something used in one place**, nothing needs installing:

```bash
git clone your-project
red your-project/logstat.red access.log
```

Relative imports resolve against the program's own file, so this works
from any directory.

**To install it** alongside the interpreter:

```bash
./setup.sh --install /usr/local
cp logstat.red parse.red /usr/local/lib/red/
```

`/usr/local/lib/red` is on the library search path, so
`import "parse.red"` resolves there from anywhere.

**To install the interpreter and your libraries together**, the layout
`setup.sh --install` produces is the one to copy:

```
/usr/local/bin/red
/usr/local/lib/red/cli.red
/usr/local/lib/red/crc32.red
/usr/local/lib/red/crc32_ext.so
/usr/local/share/red/red-legacy.jar
```

Anything you drop into `/usr/local/lib/red` is importable by bare name.
To add a directory without installing anything:

```bash
export RED_PATH=$HOME/red-libraries:/opt/red/lib
```

Before you call it done:

- `red test your-project/tests`
- the same with `--gc-stress`, and again with `--compiled`
- `program --help` prints something you would want to read
- a bad argument exits 64 and a missing file exits something other than 0
- the program works when it is started from a different directory
- `red build` produces something that runs with the sources moved away

## Where to go next

| | |
|---|---|
| [language.md](language.md) | Every form in the language, with a grammar. |
| [stdlib.md](stdlib.md) | Every built-in function and method. |
| [libraries.md](libraries.md) | Writing a library, in Red or in C++. |
| [design.md](design.md) | Why the interpreter is built the way it is. |
| [bytecode.md](bytecode.md) | The instruction set, and the compiled file format. |

Programs to read, roughly in order of size:

| | |
|---|---|
| [`examples/tour.red`](../examples/tour.red) | Every part of the language in one file. |
| [`examples/word_count.red`](../examples/word_count.red) | Tasks over a real file. |
| [`examples/echo_server.red`](../examples/echo_server.red) | A concurrent TCP server, with clients. |
| [`examples/logstat/`](../examples/logstat) | The program built here. |
| [`examples/mini_compiler.red`](../examples/mini_compiler.red) | A compiler and virtual machine for arithmetic. |
| [`selfhost/redc.red`](../selfhost/redc.red) | The Red compiler, in Red. The largest Red program there is. |
