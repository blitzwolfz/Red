# lib

The libraries that ship with Red. They are found automatically: this
directory is on the library search path whenever `red` is run from
`build/` or installed beside it. The `.red` suffix is optional in an
import.

## std

The standard library, written in Red. [docs/std.md](../docs/std.md) is
the reference.

| Module | What it is |
|---|---|
| [`std/strings`](std/strings.red) | Cutting, splitting, wrapping, and the predicates. |
| [`std/path`](std/path.red) | File paths as text. |
| [`std/fs`](std/fs.red) | Walking trees, atomic writes, globs, temporary directories. |
| [`std/time`](std/time.red) | Stopwatches, deadlines, durations, date formats. |
| [`std/url`](std/url.red) | Parsing, formatting, percent-encoding. |
| [`std/sync`](std/sync.red) | Mutex, Semaphore, WaitGroup, Once, Pool, timeouts. |
| [`std/bufio`](std/bufio.red) | Buffered reading and writing over a socket. |
| [`std/http`](std/http.red) | An HTTP server and client. |
| [`std/log`](std/log.red) | Levelled logging, as lines or as JSON. |
| [`std/testing`](std/testing.red) | Assertions, for tests that compute an answer. |

```red
import "std/http" as http;
import "std/strings" as strings;
```

## The rest

| File | What it is |
|---|---|
| [`cli.red`](cli.red) | Command line parsing: flags, options, `--`, and a usage message. |
| [`json.red`](json.red) | JSON, read and written, with sorted keys and an indented form. |
| [`crc32.red`](crc32.red) | CRC-32. Written in Red, with a C++ half in [`../ffi/crc32_ext.cpp`](../ffi/crc32_ext.cpp) that it uses when it is built. |

```red
import "cli.red" as cli;
import "json.red" as json;
import "crc32.red" as crc32;
```

[docs/libraries.md](../docs/libraries.md) explains how the search path
works and how to write a library of your own, in Red or in C++.
[`../examples/library_tour.red`](../examples/library_tour.red) uses `cli`
and `json`; [`../examples/http_server.red`](../examples/http_server.red)
uses `std/http`.
