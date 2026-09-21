// The two methods a class may define to say how its instances print and
// compare, and instances used as map keys.

class Point {
  init(x, y) {
    this.x = x;
    this.y = y;
  }
  // How this prints, anywhere a value is turned into text.
  str() { return "(${this.x}, ${this.y})"; }
  // What == means for two of these.
  eq(other) {
    return type(other) == "instance" and this.x == other.x and
           this.y == other.y;
  }
  // Not a hook, an ordinary method. A map keys instances by identity, so
  // a class that wants keying by value provides something to key on.
  key() { return "${this.x},${this.y}"; }
}

const a = Point(1, 2);
const b = Point(1, 2);
const c = Point(3, 4);

// str() is used by print, str(), repr() and interpolation alike.
print(a);                            // expect: (1, 2)
print(str(a));                       // expect: (1, 2)
print(repr(a));                      // expect: (1, 2)
print("at ${a}");                    // expect: at (1, 2)
print(a, c);                         // expect: (1, 2) (3, 4)

// Including inside a container, where it is not quoted: what str() gave
// back is the object's written form, not a string it was holding.
print([a, c]);                       // expect: [(1, 2), (3, 4)]
print({"origin": a});                // expect: {"origin": (1, 2)}
print([a].join(" and "));            // expect: (1, 2)

// eq() decides ==, and the searches that are defined in terms of it.
print(a == b);                       // expect: true
print(a == c);                       // expect: false
print(a != c);                       // expect: true
print([a, c].contains(b));           // expect: true
print([a, c].index_of(c));           // expect: 1

// A value is always equal to itself, whatever eq() says.
class Contrary {
  init() { this.n = 1; }
  eq(other) { return false; }
}
const one = Contrary();
print(one == one);                   // expect: true
print(one == Contrary());            // expect: false

// A class with neither hook is unchanged.
class Plain {
  init() { this.n = 1; }
}
print(Plain());                      // expect: Plain instance
print(Plain() == Plain());           // expect: false

// Instances are map and set keys, by identity: two objects with the same
// fields are two keys, the same way they are two objects.
const byObject = {};
byObject.set(a, "first");
print(byObject.get(a, "missing"));   // expect: first
print(byObject.get(b, "missing"));   // expect: missing
print(byObject.has(a));              // expect: true

const seen = set();
seen.add(a);
seen.add(b);
seen.add(a);
print(seen.len());                   // expect: 2

// Keying by value is a key the class hands out.
const memo = {};
memo.set(a.key(), "cached");
print(memo.get(b.key(), "missing")); // expect: cached

// An array, a map or a set is still not a key: it can be changed after
// it is used as one.
try {
  const bad = {};
  bad.set([1], "no");
} catch (e: "key") {
  print(e.message);
  // expect: A map key must be a string, number, boolean, nil, enum member or instance, got array.
}

// str() that gives back something other than a string shows that value.
class Odd {
  init() { this.n = 1; }
  str() { return 42; }
}
print(Odd());                        // expect: 42
