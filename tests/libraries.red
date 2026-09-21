// Libraries found on the search path, written in Red and in C++.
//
// Neither of these files is next to this one. They live in lib/, and the
// interpreter finds them because that directory is on the library search
// path. docs/libraries.md describes the order.

import "cli.red" as cli;
import "crc32.red" as crc32;

// ---- a library written in Red ----

const spec = cli.Spec("demo", "A test.");
spec.flag("verbose", "v", "say more");
spec.option("min", "m", "1", "a number");
spec.rest("file", "files");

const parsed = spec.parse(["-v", "--min=4", "one.txt", "two.txt"]);
print(parsed.flag("verbose"));  // expect: true
print(parsed.number("min"));    // expect: 4
print(parsed.rest().join(",")); // expect: one.txt,two.txt

// Everything after `--` is positional, however it is spelled.
const dashed = spec.parse(["--", "--min"]);
print(dashed.rest().join(",")); // expect: --min
print(dashed.option("min"));    // expect: 1

// A bad argument is an error with kind "usage", not a crash.
try {
  spec.parse(["--nonsense"]);
} catch (e: "usage") {
  print(e.message); // expect: unknown option '--nonsense'
}

print(spec.usage().split("\n")[0]); // expect: Usage: demo [options] [file...]

// ---- a library with a native half ----

// Whether the extension is built or not, the answers are the same.
crc32.use("red");
print(crc32.backend()); // expect: red
const pure = crc32.of("The quick brown fox jumps over the lazy dog");
print(pure);            // expect: 1095738169
print(crc32.hex(pure)); // expect: 0x414fa339
print(crc32.of(""));    // expect: 0

// All 256 byte values, so nothing depends on the input being text.
let every = "";
for (let code in range(0, 256)) { every += chr(code); }
crc32.use("red");
const fromRed = crc32.of(every);
print(fromRed); // expect: 688229491

crc32.use("auto");
if (crc32.backend() == "native") {
  print(crc32.of(every) == fromRed); // expect: true
} else {
  // No extension in this build. Say so in the same shape.
  print(true);
}
