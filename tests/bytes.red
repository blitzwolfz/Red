// Byte level access, which is what building binary output needs.

print(chr(65));                            // expect: A
print(chr(10) == "\n");                    // expect: true
print(chr(0).len());                       // expect: 1

print("A".code_at(0));                     // expect: 65
print("abc".code_at(-1));                  // expect: 99
print("abc".bytes());                      // expect: [97, 98, 99]

// A round trip through a number and back.
let rebuilt = "";
for (let code in "Red".bytes()) {
  rebuilt += chr(code);
}
print(rebuilt);                            // expect: Red

// Arbitrary bytes survive being written and read back, which is what a
// compiled output file depends on.
const path = "./byte_round_trip.tmp";
let blob = "";
for (let i in range(0, 256)) { blob += chr(i); }
print(blob.len());                         // expect: 256
write_file(path, blob);
const loaded = read_file(path);
print(loaded.len());                       // expect: 256
print(loaded.code_at(0), loaded.code_at(200), loaded.code_at(255));
// expect: 0 200 255
print(loaded == blob);                     // expect: true
remove_file(path);

// Splitting a value into two bytes and putting it back together, the way
// a two byte operand is encoded.
const original = 54321;
const high = original >> 8 & 255;
const low = original & 255;
print(high, low);                          // expect: 212 49
print((high << 8) | low);                  // expect: 54321

// Out of range values are refused rather than wrapped silently.
try {
  chr(256);
} catch (e) {
  print(e.message);                        // expect: chr() expects a whole number from 0 to 255, got 256.
}
try {
  "abc".code_at(9);
} catch (e) {
  print(e.message);                        // expect: code_at() index 9 out of range for length 3.
}
