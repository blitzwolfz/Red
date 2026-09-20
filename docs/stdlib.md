# Standard library

Everything here is built into the interpreter. Nothing has to be
imported.

## Output

| Function | Result |
|---|---|
| `print(...)` | Writes its arguments separated by spaces, then a newline. |
| `write(...)` | Writes its arguments with no separator and no newline. |

`print` builds the whole line and writes it once, so two tasks cannot
interleave in the middle of a line.

## Types and conversion

| Function | Result |
|---|---|
| `type(value)` | The type name, as a string. |
| `str(value)` | The value as a string, the way `print` shows it. |
| `repr(value)` | The same, but strings keep their quotes. |
| `num(value)` | A number, or `nil` when the whole string is not a number. |
| `int(value)` | The number with its fractional part removed. |
| `len(value)` | Length of a string, array or map. |

```red
print(type([]));        // array
print(num("3.5"));      // 3.5
print(num("3.5kg"));    // nil
```

## Maths

| Function | Result |
|---|---|
| `abs(n)` | Absolute value. |
| `floor(n)` `ceil(n)` | Round down, round up. |
| `sqrt(n)` | Square root. Errors on a negative number. |
| `pow(base, exponent)` | Power. |
| `min(...)` `max(...)` | Smallest, largest. |
| `range(stop)` | `[0, 1, ... stop - 1]` as an array. |
| `range(start, stop)` | From `start` up to but not including `stop`. |
| `range(start, stop, step)` | The same, with a step. A negative step counts down. |

## Errors

| Function | Result |
|---|---|
| `assert(condition)` | Raises an error when the condition is false. |
| `assert(condition, message)` | The same, with your own message. |
| `error(message)` | Builds an error value, for `throw`. |
| `error(message, payload)` | The same, carrying any value. |

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
| `platform()` | `"darwin"`, `"linux"` or `"unknown"`. |
| `cpu_count()` | Number of processors. |
| `time()` | Seconds since the epoch, with a fraction. |
| `clock()` | Processor time used, in seconds. Use this for timing. |

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
| `join()` | Waits, then gives the result. Raises if the task failed. |
| `is_done()` | Has it finished? Does not wait. |

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
const lib = ffi_open("./example_ext.so");
const hypot = lib.sym("ext_hypot");
print(hypot(3, 4));      // 5
```

[`ffi/red_ffi.h`](../ffi/red_ffi.h) defines what an extension has to
export, and [`ffi/example_ext.c`](../ffi/example_ext.c) is a working one.

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
| `split(separator)` | An array. An empty separator splits into characters. |
| `find(text)` | Index of the first match, or -1. |
| `contains(text)` | Is it in there? |
| `starts_with(text)` `ends_with(text)` | |
| `sub(start)` `sub(start, end)` | A slice. Negative counts back from the end. |
| `replace(from, to)` | Every match replaced. |
| `repeat(count)` | The string repeated. |

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
| `reduce(f)` `reduce(f, start)` | One value. |

## Map methods

| Method | Result |
|---|---|
| `len()` | Number of entries. |
| `get(key)` `get(key, fallback)` | |
| `set(key, value)` | Gives the map. |
| `has(key)` `remove(key)` | |
| `keys()` `values()` | Arrays, in no particular order. |
