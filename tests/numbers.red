// Awkward numeric literals.
//
// Every one of these is a constant, so it is written into a .redc as
// eight raw bytes and read back. The list covers the corners of the
// format: subnormals, the largest finite double, an overflow, and values
// either side of the normal/subnormal boundary. Running this suite with
// --compiled or --selfhost is what makes it a test of the writer.

print(0);                           // expect: 0
print(1);                           // expect: 1
print(-1);                          // expect: -1
print(0.5);                         // expect: 0.5
print(0.1 + 0.2);                   // expect: 0.3
print(3.141592653589793);           // expect: 3.1415926535898
print(1e15);                        // expect: 1e+15
print(1e16);                        // expect: 1e+16
print(1e21);                        // expect: 1e+21
print(6.02214076e23);               // expect: 6.02214076e+23

// The largest finite double, and one step past it.
print(1.7976931348623157e308);      // expect: 1.7976931348623e+308
print(1e400);                       // expect: inf
print(-1e400);                      // expect: -inf

// Below this the exponent stops moving and the fraction shifts instead.
print(2.2250738585072014e-308);     // expect: 2.2250738585072e-308
print(2.2250738585072011e-308);     // expect: 2.2250738585072e-308
print(1e-310);                      // expect: 1e-310
print(1e-320);                      // expect: 9.9998886718268e-321
print(5e-324);                      // expect: 4.9406564584125e-324

// Integers at and past the point where doubles stop counting by one.
print(9007199254740992);            // expect: 9.007199254741e+15
print(9007199254740993);            // expect: 9.007199254741e+15
print(4294967296);                  // expect: 4294967296

// Folding happens while compiling, so these never run as arithmetic.
print(2 * 3 + 4);                   // expect: 10
print(1 << 31);                     // expect: -2147483648
print(7 % 4);                       // expect: 3
print("a" + "b" + "c");             // expect: abc
