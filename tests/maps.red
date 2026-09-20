// Maps and their methods.

const empty = {};
print(empty, empty.len());           // expect: {} 0

const ages = {"ann": 31, "bob": 25};
print(ages.len());                   // expect: 2
print(ages["ann"]);                  // expect: 31
print(ages["nobody"]);               // expect: nil

ages["cal"] = 40;
print(ages.len());                   // expect: 3
print(ages.has("cal"));              // expect: true
print(ages.get("cal"));              // expect: 40
print(ages.get("nobody", 0));        // expect: 0
print(ages.remove("cal"));           // expect: true
print(ages.remove("cal"));           // expect: false
print(ages.len());                   // expect: 2

print(ages.keys().sort());           // expect: ["ann", "bob"]
print(ages.values().sort());         // expect: [25, 31]

// Numbers, booleans and nil work as keys too.
const mixed = {1: "one", true: "yes", nil: "nothing"};
print(mixed[1], mixed[true], mixed[nil]);  // expect: one yes nothing

// Keys are compared by value, so 1 and 1.0 are the same key.
const counts = {};
counts[1] = "first";
counts[1.0] = "second";
print(counts.len(), counts[1]);      // expect: 1 second

// Map keys must be simple values.
const bad = {};
bad[[1, 2]] = "x";
// expect runtime error: Map keys must be strings, numbers, booleans or nil.
