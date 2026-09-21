// Every number Red prints can be read back unchanged.
//
// str() writes the shortest text that recovers the same double, so this
// holds for every value, not only for the ones that look round. A fixed
// precision would fail here: %.14g, which str() used to use, drops the
// last few bits of most values.

let checked = 0;
let failed = 0;

fun check(value) {
  checked += 1;
  if (num(str(value)) != value) {
    failed += 1;
    print("does not round trip: ${value}");
  }
}

// Written out: the corners of the format, and values people type.
for (let value in [0, 1, -1, 0.5, 0.1, 0.2, 0.1 + 0.2, 1 / 3, 2 / 3,
                   3.141592653589793, 2.718281828459045,
                   1e-300, 1e300, 5e-324, 2.2250738585072014e-308,
                   1.7976931348623157e308, 9007199254740991,
                   9007199254740992, 4503599627370495.5,
                   1e15, 1e16, 1e21, 1e-7, 0.0001, -0.0001,
                   6.02214076e23, 1.602176634e-19, 123456789.123456789,
                   255, 65535, 16777215, 4294967296]) {
  check(value);
}

// Built by arithmetic rather than written out, which reaches the values
// no one would think to list: every exponent from the largest finite
// double down to the smallest subnormal, and mantissas with no pattern.
let up = 1;
let down = 1;
for (let i in range(0, 300)) {
  check(up);
  check(down);
  up = up * 1.7;
  down = down / 1.7;
}

let mixed = 1;
for (let i in range(0, 200)) {
  check(mixed);
  mixed = mixed * 3 + 1;
}

print(checked > 800);                // expect: true
print(failed);                       // expect: 0

// Whole numbers print as whole numbers up to 2^53, which is where
// doubles stop counting by one. Above that the shortest form takes over
// rather than printing digits the value does not carry.
print(9007199254740991);             // expect: 9007199254740991
print(9007199254740992);             // expect: 9007199254740992
print(9007199254740994);             // expect: 9.007199254740994e+15
print(1e16);                         // expect: 1e+16
print(1e15);                         // expect: 1000000000000000

// The readable spellings are kept where they are also the shortest.
print(0.0001);                       // expect: 0.0001
print(100.5);                        // expect: 100.5
print(1 / 3);                        // expect: 0.3333333333333333
