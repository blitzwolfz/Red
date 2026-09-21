// Maps and their methods.

const empty = {};
print(empty, empty.len()); // expect: {} 0

const ages = {"ann": 31, "bob": 25};
print(ages.len());     // expect: 2
print(ages["ann"]);    // expect: 31
print(ages["nobody"]); // expect: nil

ages["cal"] = 40;
print(ages.len());            // expect: 3
print(ages.has("cal"));       // expect: true
print(ages.get("cal"));       // expect: 40
print(ages.get("nobody", 0)); // expect: 0
print(ages.remove("cal"));    // expect: true
print(ages.remove("cal"));    // expect: false
print(ages.len());            // expect: 2

print(ages.keys().sort());   // expect: ["ann", "bob"]
print(ages.values().sort()); // expect: [25, 31]

// Numbers, booleans and nil work as keys too.
const mixed = {1: "one", true: "yes", nil: "nothing"};
print(mixed[1], mixed[true], mixed[nil]); // expect: one yes nothing

// Keys are compared by value, so 1 and 1.0 are the same key.
const counts = {};
counts[1] = "first";
counts[1.0] = "second";
print(counts.len(), counts[1]); // expect: 1 second

// Enum members are allowed as keys, because each one is a single object
// that never changes.
enum Suit { Hearts, Spades }
const bySuit = {};
bySuit[Suit.Hearts] = "red";
print(bySuit[Suit.Hearts], bySuit[Suit.Spades]); // expect: red nil

// equals() compares contents, where == compares identity.
print({"a": [1, 2]}.equals({"a": [1, 2]})); // expect: true
print({"a": 1}.equals({"a": 1, "b": 2}));   // expect: false

// clear empties a map in place, the way it does an array or a set.
const counts = {"a": 1, "b": 2};
print(counts.len());         // expect: 2
print(counts.clear().len()); // expect: 0
print(counts.has("a"));      // expect: false
counts.set("c", 3);
print(counts.get("c")); // expect: 3

// Anything else is refused.
const bad = {};
bad[[1, 2]] = "x";
// expect runtime error: A map key must be a string, number, boolean, nil, enum member or instance, got array.
