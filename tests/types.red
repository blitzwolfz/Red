// Types, checked.
//
// An annotation is not a comment. Every one of them compiles to a check,
// so a value that does not fit is reported where it crossed the line
// rather than wherever it was finally used. docs/language.md has the
// grammar.

// ---- types are values ----

print(Num);            // expect: Num
print(type_of(3));     // expect: Int
print(type_of(3.5));   // expect: Num
print(type_of("hi"));  // expect: String
print(type_of(nil));   // expect: Nil
print(type_of(true));  // expect: Bool
print(type_of([1]));   // expect: Array
print(type_of(print)); // expect: Fun

// ---- asking ----

print(3 is Num);                       // expect: true
print(3 is Int);                       // expect: true
print(3.5 is Int);                     // expect: false
print("x" is Num);                     // expect: false
print(nil is Num);                     // expect: false
print(nil is Num?);                    // expect: true
print(3 is Num?);                      // expect: true
print("x" is Any);                     // expect: true
print([1, 2] is [Num]);                // expect: true
print([1, "x"] is [Num]);              // expect: false
print({"a": 1} is { String: Num });    // expect: true
print({"a": 1} is { String: String }); // expect: false
print(set([1, 2]) is Set[Num]);        // expect: true

// A bare container name asks about the kind and nothing else, which
// costs one comparison where an element type costs a walk.
print([1, "x"] is Array); // expect: true

// ---- annotations that hold ----

fun area(w: Num, h: Num) -> Num { return w * h; }
print(area(3, 4)); // expect: 12

fun first(xs: [String]) -> String { return xs[0]; }
print(first(["a", "b"])); // expect: a

fun count(m: {String: Num}) -> Int { return len(m); }
print(count({"a": 1, "b": 2})); // expect: 2

// A default is filled in by the prologue, so it is checked like any
// other value arriving in that slot.
fun greet(who: String = "world") -> String { return "hello, ${who}"; }
print(greet());      // expect: hello, world
print(greet("Red")); // expect: hello, Red

// An optional admits nil and nothing else extra.
fun maybe(name: String?) -> String {
  if (name == nil) { return "nobody"; }
  return name;
}
print(maybe(nil));   // expect: nobody
print(maybe("Sam")); // expect: Sam

// Classes and enums are types.
class Point {
  init(x: Num, y: Num) { this.x = x; this.y = y; }
}
class Point3 < Point {
  init(x, y, z) { super.init(x, y); this.z = z; }
}
enum Colour { Red, Green }

fun shift(p: Point) -> Num { return p.x; }
print(shift(Point(5, 6)));       // expect: 5
print(shift(Point3(7, 8, 9)));   // expect: 7
print(Point(1, 2) is Point);     // expect: true
print(Point3(1, 2, 3) is Point); // expect: true
print(Point(1, 2) is Point3);    // expect: false
print(Colour.Red is Colour);     // expect: true
print(type_of(Colour.Green));    // expect: Colour

// A function type asks about the shape of what it is given.
fun apply(f: fun(Num) -> Num, x: Num) -> Num { return f(x); }
fun double(n: Num) -> Num { return n * 2; }
print(apply(double, 21)); // expect: 42

// ---- annotations that do not ----

fun kindOf(body) -> String {
  try {
    body();
  } catch (e: "type") {
    return e.message;
  }
  return "no error";
}

print(kindOf(fun () { area("3", 4); }));
// expect: 'w': expected Num, got string.
print(kindOf(fun () { first([1, 2]); }));
// expect: 'xs': expected [String], but element 0 is number.
print(kindOf(fun () { count({"a": "b"}); }));
// expect: 'm': expected {String: Num}, but the value at "a" is string.
print(kindOf(fun () { maybe(3); }));
// expect: 'name': expected String?, got number.
print(kindOf(fun () { shift(Point3(1, 2, 3).x); }));
// expect: 'p': expected Point, got number.
print(kindOf(fun () { apply(area, 1); }));
// expect: 'f': expected fun(Num) -> Num, but that function takes 2 arguments.

// A returned value is checked on the way out, and so is the nil a
// function gives back when it runs off the end. Both go through a value
// the compiler cannot see, because a literal that cannot fit is caught
// while compiling instead: tests/type_literals.red covers that.
fun text() { return "no"; }
fun wrong() -> Num { return text(); }
fun missing() -> Num { }
print(kindOf(fun () { wrong(); }));   // expect: expected Num, got string.
print(kindOf(fun () { missing(); })); // expect: expected Num, got nil.

// An annotation holds for as long as the name does, so assigning to one
// is checked the same way declaring it was.
print(kindOf(fun () { let n: Num = 1; n = text(); print(n); }));
// expect: expected Num, got string.

// A name that is not a type says so, rather than looking like a value
// that does not fit.
print(kindOf(fun () { let t: Nonesuch = text(); print(t); }));
// expect: there is no type called Nonesuch.

// ---- Any ----

// Any is what an unannotated name already means, so it admits
// everything and compiles to nothing at all.
fun anything(x: Any) -> Any { return x; }
print(anything(1));     // expect: 1
print(anything("two")); // expect: two
print(anything(nil));   // expect: nil
