// A tight arithmetic loop. Measures the dispatch loop itself: local reads
// and writes, comparisons and jumps.
let total = 0;
for (let i = 0; i < 20000000; i = i + 1) {
  total = total + i % 7;
}
print(total);
