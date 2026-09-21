// Using libraries.
//
//   red examples/library_tour.red --loud one.txt two.txt
//
// Two libraries, neither of them next to this file. Both are found on the
// library search path, which docs/libraries.md describes.
//
//   cli.red    written entirely in Red
//   crc32.red  written in Red, with a C++ extension it uses when present

import "cli.red" as cli;
import "crc32.red" as crc32;

// ---- where the interpreter looks -------------------------------------

print("library path:");
for (let directory in library_paths()) { print("  " + directory); }
print("");

// ---- a library written in Red ----------------------------------------

const spec = cli.Spec("library_tour", "Shows how a Red library is used.");
spec.flag("loud", "l", "print each name in capitals");
spec.option("repeat", "r", "1", "how many times to print each name");
spec.rest("name", "names to print");

let options = nil;
try {
  options = spec.parse(args());
} catch (e: "usage") {
  print(e.message);
  print(spec.usage());
  exit(64);
}

if (options.flag("help")) {
  print(spec.usage());
  exit(0);
}

let names = options.rest();
if (names.len() == 0) { names = ["ada", "grace"]; }

for (let name in names) {
  let shown = name;
  if (options.flag("loud")) { shown = name.upper(); }
  for (let i in range(0, options.number("repeat"))) { print(shown); }
}
print("");

// ---- a library with a native half ------------------------------------

// The same call whichever half answers it. `backend()` reports which one
// did, and there is no other way to tell.
print("crc32 backend: " + crc32.backend());
for (let name in names) {
  print(name.pad_right(10) + crc32.hex(crc32.of(name)));
}

// Pinning the Red half proves the two agree.
const native = crc32.of("checksum me");
crc32.use("red");
print(crc32.of("checksum me") == native);
crc32.use("auto");
