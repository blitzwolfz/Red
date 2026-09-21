// CRC-32, the reflected polynomial that zlib and PNG use.
//
//   import "crc32.red" as crc32;
//   print(crc32.of("hello"));          // 907060870
//   print(crc32.hex(crc32.of("hello"))); // 0x3610a686
//
// This library has two halves. The work is done by a C++ extension when
// one is present, and by the Red below when it is not. Which one ran is
// visible through `backend()`, and both produce the same numbers.
//
// The pattern is the point: a library is Red, and native code is an
// optimisation it can do without. docs/libraries.md walks through it.

// The table is built once, on first use, because building it costs 256
// small loops and most programs never ask for a checksum at all.
let table = nil;

fun buildTable() {
  const entries = [];
  for (let i in range(0, 256)) {
    let value = i;
    for (let bit in range(0, 8)) {
      if (value & 1 != 0) {
        // >> on a negative number keeps the sign, so the top bit is
        // cleared by hand before the shift.
        value = (value >> 1 & 2147483647) ^ -306674912;
      } else {
        value = value >> 1 & 2147483647;
      }
    }
    entries.push(value);
  }
  return entries;
}

// The extension, or nil when there is none. ffi_open searches the library
// path, so a bare name is enough wherever the interpreter was installed.
let native = nil;
let tried = false;
// "auto", "native" or "red". Set by use().
let preference = "auto";

fun loadNative() {
  if (preference == "red") { return nil; }
  if (tried) { return native; }
  tried = true;
  try {
    const lib = ffi_open("crc32_ext.so");
    const version = lib.sym("crc32_version");
    if (version() != "1") { return nil; }
    native = lib.sym("crc32_of");
  } catch (e: "ffi") {
    // No extension installed. The Red path below is the whole library.
    native = nil;
  }
  return native;
}

// Pins which half does the work: "native", "red", or "auto" to use the
// extension when there is one. Asking for "native" when no extension is
// installed is an error rather than a silent fallback.
fun use(which) {
  preference = which;
  if (which == "native" and loadNative() == nil) {
    preference = "auto";
    throw error("the crc32 extension is not installed", nil, "ffi");
  }
  return which;
}

// "native" or "red", whichever is doing the work.
fun backend() {
  if (loadNative() == nil) { return "red"; }
  return "native";
}

// The checksum of a string, as a number from 0 to 4294967295.
fun of(text) {
  const fast = loadNative();
  if (fast != nil) { return fast(text); }

  if (table == nil) { table = buildTable(); }
  let crc = -1;
  for (let byte in text.bytes()) {
    crc = table[(crc ^ byte) & 255] ^ (crc >> 8 & 16777215);
  }
  crc = crc ^ -1;
  // Bitwise results are signed 32 bit. Checksums are conventionally
  // unsigned, so the negative half is shifted up.
  if (crc < 0) { return crc + 4294967296; }
  return crc;
}

// The same value written the way checksums are usually quoted.
fun hex(value) {
  const digits = "0123456789abcdef";
  let out = "";
  let left = value;
  for (let i in range(0, 8)) {
    out = digits[floor(left % 16)] + out;
    left = floor(left / 16);
  }
  return "0x" + out;
}
