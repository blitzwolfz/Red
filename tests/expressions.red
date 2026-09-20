// Arithmetic, comparison and the truthiness rules.

print(1 + 2 * 3);            // expect: 7
print((1 + 2) * 3);          // expect: 9
print(7 / 2);                // expect: 3.5
print(7 % 3);                // expect: 1
print(-3 + 1);               // expect: -2
print(2 - -2);               // expect: 4

print(1 < 2);                // expect: true
print(2 <= 2);               // expect: true
print(3 > 4);                // expect: false
print(3 >= 4);               // expect: false
print(1 == 1);               // expect: true
print(1 != 1);               // expect: false

// Strings compare in dictionary order.
print("apple" < "banana");   // expect: true
print("b" > "a");            // expect: true
print("abc" == "abc");       // expect: true

// Only nil and false are falsey.
print(!nil);                 // expect: true
print(!false);               // expect: true
print(!0);                   // expect: false
print(!"");                  // expect: false

// and/or return the deciding operand and do not evaluate further.
print(nil or "fallback");    // expect: fallback
print("first" or "second");  // expect: first
print(false and "never");    // expect: false
print(true and "yes");       // expect: yes
