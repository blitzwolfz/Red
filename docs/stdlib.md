# Standard library

Everything here is built into the interpreter. Nothing has to be
imported.

## Output

| Function | Result |
|---|---|
| `print(...)` | Writes its arguments separated by spaces, then a newline. |
| `write(...)` | Writes its arguments with no separator and no newline. |
| `eprint(...)` | The same as `print`, on the error stream. |
| `ewrite(...)` | The same as `write`, on the error stream. |

`print` builds the whole line and writes it once, so two tasks cannot
interleave in the middle of a line.

A program's results go to standard output and its complaints go to
standard error, so that whoever runs it can keep the two apart.

```bash
red report.red > report.txt      # the results
red report.red 2> problems.txt   # what went wrong
```

## Types and conversion

| Function | Result |
|---|---|
| `type(value)` | The type name, as a string. |
| `str(value)` | The value as a string, the way `print` shows it. |
| `repr(value)` | The same, but strings keep their quotes. |
| `num(value)` | A number, or `nil` when the whole string is not a number. |
| `int(value)` | The number with its fractional part removed. |
| `len(value)` | Length of a string, array, map or enum. |
| `chr(code)` | A one byte string, for a value from 0 to 255. |
| `char(code)` | A one character string, for a code point up to 10ffff. |
| `set()` | An empty set. |
| `set(source)` | A set from an array, a set or a string. |

```red
print(type([]));        // array
print(num("3.5"));      // 3.5
print(num("3.5kg"));    // nil
```

`chr` and `code_at` are what let Red build binary data. Strings hold
arbitrary bytes, including zero, and `write_file` writes them unchanged.

```red
let blob = "";
for (let code in [82, 101, 100]) { blob += chr(code); }
print(blob);                 // Red
print(blob.bytes());         // [82, 101, 100]
```

## Text and bytes

A string is a sequence of bytes and may hold anything, text or not. That
leaves two ways of looking at one. Red keeps them apart and does not
guess which you meant.

| | Bytes | Characters |
|---|---|---|
| Build one | `chr(code)` | `char(code)` |
| Take apart | `text.bytes()` | `text.chars()` |
| Numbers | `text.code_at(i)` | `text.code_points()` |
| How many | `text.len()` | `text.char_len()` |
| One at a time | `text[i]` | `for (let c in text)` |

```red
const greeting = "héllo";
print(greeting.len());          // 6, because é takes two bytes
print(greeting.char_len());     // 5
print(greeting.chars());        // ["h", "é", "l", "l", "o"]
print(greeting.code_points());  // [104, 233, 108, 108, 111]
print(greeting[1]);             // half of é, as a byte
print(greeting.chars()[1]);     // é
```

Characters are decoded as UTF-8. A byte that does not begin a well formed
sequence comes back on its own, so `text.chars().join("")` always gives
back exactly what it started with, whether or not the string was text.

A code point can also be written into a string literal directly, with
four hex digits or with braces around one to six:

```red
print("caf\u00e9");              // café
print("\u{1f600}");              // 😀
```

`upper()` and `lower()` cover the whole of Unicode. That includes the
characters whose case changes their length, so `"straße".upper()` is
`"STRASSE"` and `"ﬁle".upper()` is `"FILE"`. Bytes that are not valid
UTF-8 are copied through untouched.

What is not covered is case that depends on the language: Turkish
dotless i stays dotless, and Greek final sigma is not distinguished.
Doing those needs a locale, and Red has none.

## Maths

| Function | Result |
|---|---|
| `abs(n)` | Absolute value. |
| `floor(n)` `ceil(n)` | Round down, round up. |
| `round(n)` | Nearest whole number. A half goes away from zero. |
| `sign(n)` | `-1`, `0` or `1`. |
| `sqrt(n)` | Square root. Errors on a negative number. |
| `pow(base, exponent)` | Power. |
| `exp(n)` | `e` to the power of `n`. |
| `log(n)` | Natural logarithm. |
| `log(n, base)` | Logarithm to any base. |
| `hypot(a, b)` | `sqrt(a*a + b*b)`, without overflowing on the way. |
| `sin(n)` `cos(n)` `tan(n)` | In radians. |
| `asin(n)` `acos(n)` `atan(n)` | The inverses. |
| `atan(y, x)` | The angle to a point, taking its quadrant into account. |
| `min(...)` `max(...)` | Smallest, largest. |
| `range(stop)` | `[0, 1, ... stop - 1]` as an array. |
| `range(start, stop)` | From `start` up to but not including `stop`. |
| `range(start, stop, step)` | The same, with a step. A negative step counts down. |

| Constant | |
|---|---|
| `PI` | 3.141592653589793 |
| `E` | 2.718281828459045 |

An argument a function has no answer for raises an error with kind
`"domain"`. It does not quietly produce `nan`.

```red
try {
  log(-1);
} catch (e: "domain") {
  print(e.message);            // log() of a negative number: -1.
}
```

## Randomness

| Function | Result |
|---|---|
| `rand()` | A fraction from 0 up to but not including 1. |
| `rand(stop)` | A whole number from 0 up to but not including `stop`. |
| `rand(start, stop)` | A whole number in that range, `stop` excluded. |
| `rand_seed(n)` | Fixes the sequence, so a run can be repeated. |

The upper end is left out, the same way `range` leaves it out, so
`rand(items.len())` indexes an array and never runs off the end.

```red
const roll = rand(1, 7);              // 1 to 6
const pick = items[rand(items.len())];
```

```red
rand_seed(1234);                      // the same run every time
```

## Errors

| Function | Result |
|---|---|
| `assert(condition)` | Raises an error when the condition is false. |
| `assert(condition, message)` | The same, with your own message. |
| `error(message)` | Builds an error value, for `throw`. |
| `error(message, payload)` | The same, carrying any value. |
| `error(message, payload, kind)` | The same, with an explicit kind. |

## Input

| Function | Result |
|---|---|
| `input()` | Reads one line from the terminal, without the newline. `nil` at end of input. |
| `input(prompt)` | Writes the prompt first. |

## Files

| Function | Result |
|---|---|
| `read_file(path)` | The whole file as a string, or `nil`. |
| `write_file(path, text)` | Writes, replacing the file. `true` on success. |
| `append_file(path, text)` | Adds to the end of the file. |
| `open(path, mode)` | A file handle. Mode defaults to `"r"`. |
| `remove_file(path)` | Deletes the file. |
| `exists(path)` | Is there a file or directory at this path? |
| `is_file(path)` `is_dir(path)` | Which of the two it is. |
| `file_size(path)` | Size in bytes, or `nil`. |
| `modified(path)` | When it was last written, in `time()` seconds, or `nil`. |
| `rename(from, to)` | Moves or renames. `true` on success. |

## Directories

| Function | Result |
|---|---|
| `list_dir(path)` | The names inside, sorted, without `.` and `..`. `nil` when the directory cannot be read. |
| `mkdir(path)` | Makes it, and any parent it needs. `true` when it exists afterwards. |
| `remove_dir(path)` | Removes it. It has to be empty. |

`list_dir` sorts, so walking a directory does the same thing twice
running. A path that is not there gives `nil`, not an empty array,
which keeps a missing directory and an empty one apart.

```red
for (let name in list_dir("logs")) {
  const path = "logs/" + name;
  if (is_file(path) and name.ends_with(".log")) {
    print(name.pad_right(24) + str(file_size(path)));
  }
}
```

`mkdir` makes parents, like `mkdir -p`, and succeeds for a directory that
is already there, because the caller wanted it to exist and it does.
`remove_dir` refuses a directory with anything in it: deleting a tree is a
decision a program should make one file at a time.

File methods: `read` `read_line` `lines` `write` `flush` `close`
`is_open`.

```red
const handle = open("notes.txt", "r");
const lines = handle.lines();
handle.close();
```

`read_line` gives `nil` at the end of the file, so a blank line and the
end of the file are different.

## Process and system

| Function | Result |
|---|---|
| `args()` | Arguments after the script name, as an array. |
| `env(name)` | An environment variable, or `nil`. |
| `env(name, fallback)` | The variable, or the fallback. |
| `set_env(name, value)` | Sets one. |
| `exit(code)` | Stops the program at once. Code defaults to 0. |
| `cwd()` | The working directory. |
| `source_path()` | Path of the file this call is written in. |
| `source_dir()` | The directory that file is in. |
| `library_paths()` | Where `import` and `ffi_open` look for a name. |
| `platform()` | `"darwin"`, `"linux"` or `"unknown"`. |
| `cpu_count()` | Number of processors. |
| `time()` | Seconds since the epoch, with a fraction. |
| `clock()` | Processor time used, in seconds. Use this for timing. |

## Dates

| Function | Result |
|---|---|
| `date()` | Now, as a map of parts, in local time. |
| `date(seconds)` | The same for a given moment. |
| `date(seconds, true)` | The same in UTC. |
| `format_time(seconds, pattern)` | Formatted with `strftime` patterns, in local time. |
| `format_time(seconds, pattern, true)` | The same in UTC. |

The map holds `year`, `month` (1 to 12), `day`, `hour`, `minute`,
`second`, `weekday` (0 for Sunday) and `yearday` (1 to 366).

```red
const now = date();
print("${now["year"]}-${now["month"]}-${now["day"]}");
print(format_time(time(), "%Y-%m-%d %H:%M:%S"));
print(format_time(0, "%Y-%m-%d", true));        // 1970-01-01
```

`source_dir()` is how a library reaches a file that ships with it. It
answers for the file the call is written in, not for the program that
imported it, so it keeps working whatever directory the program was
started from.

```red
const words = read_file(source_dir() + "/words.txt");
```

## Memory

| Function | Result |
|---|---|
| `collect()` | Runs a collection now. |
| `gc_info()` | A map with `bytes`, `next`, `collections` and `peak`. |

```red
const before = gc_info()["bytes"];
buildSomethingLarge();
collect();
print("kept ${gc_info()["bytes"] - before} bytes");
```

## Tasks and channels

| Function | Result |
|---|---|
| `chan()` | An unbuffered channel. A send waits for a receive. |
| `chan(capacity)` | A channel that can hold `capacity` values. |
| `sleep(seconds)` | Pauses this task. Other tasks keep running. |

`spawn call(...)` starts a task. It is a keyword, not a function.

Channel methods:

| Method | Result |
|---|---|
| `send(value)` | Waits for room, then queues the value. |
| `recv()` | Waits for a value. `nil` when the channel is closed and empty. |
| `try_recv()` | A value if one is waiting, `nil` if not. Never waits. |
| `close()` | No more sends. Waiting receivers wake up. |
| `len()` | How many values are queued. |
| `is_closed()` | Has it been closed? |

Task methods:

| Method | Result |
|---|---|
| `join()` | Waits, then gives the result. Raises what the task raised if it failed. |
| `is_done()` | Has it finished? Does not wait. |

## Regular expressions

| Function | Result |
|---|---|
| `regex(pattern)` | A compiled pattern. |
| `regex(pattern, flags)` | The same, with `i`, `m` and `s` in any order. |

| Flag | |
|---|---|
| `i` | Ignore case, using the same mappings as `upper()` and `lower()`. |
| `m` | `^` and `$` also meet a line break. |
| `s` | `.` also meets a newline. |

| Method | Result |
|---|---|
| `test(text)` `test(text, from)` | Is there a match? |
| `find(text)` `find(text, from)` | The first match as a map, or `nil`. |
| `find_all(text)` | Every match, as an array of maps. |
| `replace(text, with)` | Every match replaced. `$1` to `$9` are the groups, `$0` the whole match, `$$` a dollar. |
| `replace(text, with, count)` | The same, stopping after `count`. |
| `split(text)` | The pieces between matches. A capture group in the pattern is kept. |
| `pattern()` `flags()` `groups()` | What it was built from, and how many groups it has. |

A match is a map with `start` and `end` as byte offsets, `text` for what
was matched, and `groups` for what each group caught, with `nil` for a
group that took no part.

```red
const date = regex("(\\d{4})-(\\d{2})-(\\d{2})");
const found = date.find("due 2024-02-29 at noon");
print(found["text"]);              // 2024-02-29
print(found["groups"]);            // ["2024", "02", "29"]
print(found["start"]);             // 4
print(date.replace("2024-02-29", "$3/$2/$1"));   // 29/02/2024
```

Supported: `.` `*` `+` `?` `{n}` `{n,}` `{n,m}`, the lazy forms `*?`
`+?` `??`, `|`, groups `( )` and `(?: )`, classes `[a-z]` `[^a-z]`, the
shorthands `\d \w \s \D \W \S`, the anchors `^ $ \b \B`, and `\u` for a
code point. Patterns work in characters, not bytes, so `.` matches one
character however many bytes it takes.

**Not supported: backreferences and lookaround.** They are what make a
pattern stop being regular, and supporting them means backtracking, and
backtracking means a pattern like `(a+)+b` can take longer than the age
of the universe on forty characters. This engine follows every
alternative at the same time instead, so every pattern runs in time
proportional to the length of the text. `(a+)+b` against five thousand
characters finishes in a millisecond here.

A pattern that cannot be compiled raises an error with kind `"regex"`
saying what was wrong. It does not wait and fail to match at run time.

## Running other programs

| Function | Result |
|---|---|
| `run(argv)` | Runs a program. `argv` is an array: the program, then its arguments. |
| `run(argv, input)` | The same, with `input` on its standard input. |
| `shell(command)` | Runs the text through `/bin/sh`. |
| `shell(command, input)` | The same, with input. |
| `which(name)` | Where a program is, or `nil`. |

Both give back a map with `code`, `out` and `err`.

```red
const result = run(["git", "rev-parse", "HEAD"]);
if (result["code"] != 0) {
  eprint(result["err"].trim());
  exit(1);
}
print(result["out"].trim());
```

**Prefer `run` to `shell`.** `run` hands the array to the operating
system as it stands: nothing is split, expanded or quoted, so a value
that came from outside the program is an argument and can never become
another command.

```red
run(["echo", untrusted]);        // always one argument
shell("echo " + untrusted);      // whatever the shell makes of it
```

`shell` is for when a pipeline, a glob or a redirection is what was
actually wanted. The runtime lock is released while either waits, so
other tasks keep running.

A program that cannot be started at all raises an error with kind
`"process"`, which is different from one that started and exited
non-zero. A program killed by a signal reports `128` plus the signal
number, the way a shell does.

## Network

| Function | Result |
|---|---|
| `tcp_listen(port)` | A listening socket. Port 0 asks the system to choose. |
| `tcp_listen(port, backlog)` | The same, with a queue length. |
| `tcp_connect(host, port)` | A connected socket. |

Socket methods:

| Method | Result |
|---|---|
| `accept()` | Waits for a connection and gives a new socket. |
| `read()` | Up to 4096 bytes as a string. `nil` when the peer closed. |
| `read(count)` | Up to `count` bytes. |
| `write(...)` | Sends everything. Gives the number of bytes sent. |
| `close()` | Closes it. |
| `port()` | The port this socket is bound to. |
| `fd()` | The underlying file descriptor. |

Every call that can wait releases the runtime lock first, so other tasks
keep running.

## Extensions

| Function | Result |
|---|---|
| `ffi_open(path)` | Loads a shared library. |

Library methods: `sym(name)` gives a callable, `close()` unloads it.

```red
const lib = ffi_open("example_ext.so");
const hypot = lib.sym("ext_hypot");
print(hypot(3, 4));      // 5
```

A name with no `/` in it is looked for on the same search path as
`import`; a name with one is used exactly as written. A missing file
raises an error with kind `"ffi"`, which lets a library treat its
native half as optional.

[`ffi/red_ffi.h`](../ffi/red_ffi.h) is the contract in C and
[`ffi/red_ffi.hpp`](../ffi/red_ffi.hpp) is a header-only layer over it for
C++. [docs/libraries.md](libraries.md) walks through writing one.

## Running v1 programs

| Function | Result |
|---|---|
| `legacy(path)` | Runs a script on the v1 interpreter. Output goes to the terminal. Gives the exit code. |
| `legacy_output(path)` | Runs it and gives its output as a string. |
| `legacy_available()` | Is the v1 interpreter built? |

## String methods

| Method | Result |
|---|---|
| `len()` | Length in bytes. |
| `upper()` `lower()` | Case conversion. |
| `trim()` | Without leading and trailing whitespace. |
| `trim_start()` `trim_end()` | One end only. |
| `split(separator)` | An array. An empty separator splits into characters. |
| `find(text)` | Index of the first match, or -1. |
| `contains(text)` | Is it in there? |
| `starts_with(text)` `ends_with(text)` | |
| `code_at(index)` | The byte at an index, as a number. Negative counts back from the end. |
| `bytes()` | Every byte as an array of numbers. |
| `chars()` | Every character as an array of strings, decoded as UTF-8. |
| `code_points()` | Every character as an array of code points. |
| `char_len()` | How many characters, as opposed to bytes. |
| `sub(start)` `sub(start, end)` | A slice. Negative counts back from the end. |
| `replace(from, to)` | Every match replaced. |
| `repeat(count)` | The string repeated. |
| `pad_left(width)` `pad_left(width, fill)` | Padded on the left to a width. Fill defaults to a space. |
| `pad_right(width)` `pad_right(width, fill)` | Padded on the right. |

Anything already at least that wide is returned unchanged, so one long
entry never collapses a column.

```red
for (let [name, size] in rows) {
  print(name.pad_right(10) + size.pad_left(5));
}
```

## Array methods

| Method | Result |
|---|---|
| `len()` | Number of elements. |
| `push(...)` | Adds to the end. Gives the array. |
| `pop()` | Removes and gives the last element. |
| `insert(index, value)` `remove(index)` | |
| `slice(start)` `slice(start, end)` | A new array. |
| `join(separator)` | A string. |
| `contains(value)` `index_of(value)` | |
| `reverse()` `clear()` | In place. |
| `sort()` | Numbers or strings, in place. |
| `sort(compare)` | Uses your function. It gets two elements and returns true when the first comes earlier. |
| `map(f)` `filter(f)` | A new array. |
| `any(f)` `all(f)` | Does any element pass the test, do all of them? |
| `find(f)` | The first element that passes, or `nil`. |
| `find_index(f)` | Where it was, or `-1`. |
| `reduce(f)` `reduce(f, start)` | One value. |

`any`, `all` and `find` stop at the first answer. `find_index` is how an
element that is `nil` is told apart from no match at all.

## Map methods

| Method | Result |
|---|---|
| `len()` | Number of entries. |
| `get(key)` `get(key, fallback)` | |
| `set(key, value)` | Gives the map. |
| `has(key)` `remove(key)` | |
| `clear()` | Empties it. |
| `keys()` `values()` | Arrays, in no particular order. |
| `entries()` | Each entry as a two element array, for `for (let [k, v] in m.entries())`. |

## Set methods

| Method | Result |
|---|---|
| `add(...)` | Adds values. Gives the set. |
| `remove(value)` | `true` when it was there. |
| `has(value)` | Is it in the set? |
| `len()` | How many values. |
| `items()` | The values as an array. |
| `clear()` | Empties it. |
| `union(other)` `intersect(other)` `difference(other)` | A new set. |
| `equals(other)` | Compares contents. |

## Enum methods

| Method | Result |
|---|---|
| `values()` | Members, in declaration order. |
| `from(value)` | The member with that value, or `nil`. |
| `name()` | The enum's own name. |
| `len()` | How many members. |

A member has `name`, `value` and `owner`.
