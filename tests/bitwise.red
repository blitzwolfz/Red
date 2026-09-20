// Bitwise operators. They work on 32 bit integers, so a number is
// truncated and wrapped into that range first.

print(12 & 10);                  // expect: 8
print(12 | 10);                  // expect: 14
print(12 ^ 10);                  // expect: 6
print(~0);                       // expect: -1
print(~5);                       // expect: -6
print(1 << 8);                   // expect: 256
print(256 >> 4);                 // expect: 16

// The shift is arithmetic, so the sign is kept.
print(-16 >> 2);                 // expect: -4

// Shift counts are masked to 0 to 31, so shifting by 32 is defined.
print(1 << 32);                  // expect: 1
print(1 << 33);                  // expect: 2

// Values wrap into 32 bits.
print(4294967296 | 0);           // expect: 0
print(4294967295 | 0);           // expect: -1
print(2147483647 + 1 | 0);       // expect: -2147483648

// Fractions are truncated towards zero.
print(7.9 & 15);                 // expect: 7
print(-7.9 | 0);                 // expect: -7

// Bitwise binds tighter than comparison, which avoids the trap where
// `a & b == c` silently means `a & (b == c)`.
print(12 & 10 == 8);             // expect: true
print(1 | 2 == 3);               // expect: true

// Splitting a number into two bytes, which is what emitting a two byte
// operand needs.
const value = 0 + 4660;
print(value >> 8 & 255);         // expect: 18
print(value & 255);              // expect: 52

// Errors are reported, not guessed at.
try {
  print("x" & 1);
} catch (e) {
  print(e.message);              // expect: Bitwise operators need two numbers, got string and number.
}
