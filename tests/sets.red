// A set holds distinct values. Membership follows the same rules as map
// keys, so anything that can key a map can go in a set.

const empty = set();
print(empty, len(empty));                // expect: set() 0

const numbers = set([3, 1, 2, 3, 1]);
print(numbers.len());                    // expect: 3
print(numbers.items().sort());           // expect: [1, 2, 3]
print(numbers.has(2), numbers.has(9));   // expect: true false

numbers.add(4);
numbers.add(5, 6);
print(numbers.items().sort());           // expect: [1, 2, 3, 4, 5, 6]
print(numbers.remove(1), numbers.remove(1));   // expect: true false
print(numbers.len());                    // expect: 5

// Building from a string gives its distinct characters.
print(set("banana").items().sort());     // expect: ["a", "b", "n"]

// Copying a set leaves the original alone.
const original = set([1, 2]);
const copy = set(original);
copy.add(3);
print(original.items().sort(), copy.items().sort());
// expect: [1, 2] [1, 2, 3]

// Combining. None of these change their operands.
const a = set([1, 2, 3]);
const b = set([3, 4]);
print(a.union(b).items().sort());        // expect: [1, 2, 3, 4]
print(a.intersect(b).items().sort());    // expect: [3]
print(a.difference(b).items().sort());   // expect: [1, 2]
print(a.items().sort(), b.items().sort());   // expect: [1, 2, 3] [3, 4]

// equals() compares contents, like arrays and maps.
print(set([1, 2]).equals(set([2, 1])));  // expect: true
print(set([1]).equals(set([1, 2])));     // expect: false
print(set([1]).equals([1]));             // expect: false

// Mixed contents, including enum members.
enum Suit { Hearts, Spades }
const mixed = set([1, "one", true, nil, Suit.Hearts]);
print(mixed.len());                      // expect: 5
print(mixed.has(Suit.Hearts), mixed.has(Suit.Spades));   // expect: true false

// for-in walks the members.
let seen = [];
for (let item in set(["b", "a", "c"])) { seen.push(item); }
print(seen.sort());                      // expect: ["a", "b", "c"]

const cleared = set([1, 2]);
cleared.clear();
print(cleared.len());                    // expect: 0

print(type(set()));                      // expect: set

// A set cannot hold something that could change underneath it.
try {
  set([[1, 2]]);
} catch (e: "key") {
  print(e.message);
  // expect: A set can hold strings, numbers, booleans, nil, enum members and instances, not array.
}
