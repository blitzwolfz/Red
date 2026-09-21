// Padding, so that columns line up without building spaces by hand.

print("|" + "7".pad_left(4) + "|");  // expect: |   7|
print("|" + "7".pad_right(4) + "|"); // expect: |7   |
print("7".pad_left(3, "0"));         // expect: 007
print("ab".pad_right(5, "."));       // expect: ab...

// Anything already wide enough is returned unchanged, so a long entry
// never collapses a column.
print("toolong".pad_left(3)); // expect: toolong
print("exact".pad_right(5));  // expect: exact
print("".pad_left(3, "-"));   // expect: ---

// A small table, which is what this is for.
const rows = [["op", "bytes"], ["CONSTANT", "3"], ["ADD", "1"]];
for (let [name, size] in rows) {
  print(name.pad_right(10) + size.pad_left(5));
}
// expect: op        bytes
// expect: CONSTANT      3
// expect: ADD           1

// The fill has to be a single character.
try {
  "x".pad_left(5, "ab");
} catch (e: "value") {
  print(e.message); // expect: pad_left() fill must be one character, got 2.
}
