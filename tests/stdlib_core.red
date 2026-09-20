// The free functions every module can see.

print(type(1), type("s"), type(true), type(nil));   // expect: number string bool nil
print(type([]), type({}), type(print));             // expect: array map function

print(str(42), str(true), str(nil));                // expect: 42 true nil
print(repr("quoted"));                              // expect: "quoted"
print(num("3.5"), num("12"), num("nope"));          // expect: 3.5 12 nil
print(int(3.9), int(-3.9));                         // expect: 3 -3

print(len("abc"), len([1, 2]), len({"a": 1}));      // expect: 3 2 1

print(abs(-5), floor(1.7), ceil(1.2));              // expect: 5 1 2
print(sqrt(16), pow(2, 10));                        // expect: 4 1024
print(min(3, 1, 2), max(3, 1, 2));                  // expect: 1 3

print(range(4));                                    // expect: [0, 1, 2, 3]
print(range(2, 5));                                 // expect: [2, 3, 4]
print(range(0, 10, 3));                             // expect: [0, 3, 6, 9]
print(range(3, 0, -1));                             // expect: [3, 2, 1]

// Whole numbers print without a decimal point.
print(1.0, 2.5, 1e3);                               // expect: 1 2.5 1000

// assert passes quietly and fails loudly.
assert(true);
assert(1 == 1, "never shown");
print("assertions passed");                         // expect: assertions passed

// gc_info reports the state of the heap.
const info = gc_info();
print(info.has("bytes"), info.has("collections"));  // expect: true true
collect();
print(type(gc_info()["bytes"]));                    // expect: number
