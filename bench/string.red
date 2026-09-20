// String building and inspection. Measures interning, hashing and the
// allocation path for short lived strings.
let parts = [];
for (let i = 0; i < 500000; i = i + 1) {
  parts.push("item-${i}-${i % 13}");
}
const joined = parts.join(",");
let hits = 0;
for (let i = 0; i < parts.len(); i = i + 1) {
  if (parts[i].ends_with("-0")) { hits = hits + 1; }
}
print(joined.len(), hits);
