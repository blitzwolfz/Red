# lib

Libraries that ship with Red. They are found automatically: this
directory is on the library search path whenever `red` is run from
`build/`, so `import "cli.red"` works from anywhere.

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
[`../examples/library_tour.red`](../examples/library_tour.red) uses both
of these.
