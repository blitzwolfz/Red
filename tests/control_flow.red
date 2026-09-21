// if, while, for, break and continue.

if (true) { print("then"); } else { print("else"); }  // expect: then
if (false) { print("then"); } else { print("else"); } // expect: else
if (nil) { print("no"); }
if (1 < 2) print("no braces"); // expect: no braces

let i = 0;
while (i < 3) {
  print("while ${i}");
  i = i + 1;
}
// expect: while 0
// expect: while 1
// expect: while 2

for (let j = 0; j < 3; j = j + 1) {
  print("for ${j}");
}
// expect: for 0
// expect: for 1
// expect: for 2

// break leaves the loop, continue skips to the next step.
for (let k = 0; k < 10; k = k + 1) {
  if (k == 1) { continue; }
  if (k == 4) { break; }
  print("k ${k}");
}
// expect: k 0
// expect: k 2
// expect: k 3

// continue in a while loop must not skip the update, which is why the
// update comes before it here.
let n = 0;
let evens = [];
while (n < 6) {
  n = n + 1;
  if (n % 2 == 1) { continue; }
  evens.push(n);
}
print(evens); // expect: [2, 4, 6]

// A for loop with no clauses runs until something breaks it.
let count = 0;
for (;;) {
  count = count + 1;
  if (count == 3) { break; }
}
print(count); // expect: 3
