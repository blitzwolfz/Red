// String literals, escapes, interpolation and methods.

print("hello" + " " + "world"); // expect: hello world
print("tab:\tdone");            // expect: tab:	done
print("quote:\"q\"");           // expect: quote:"q"

const name = "Red";
const version = 2;
print("${name} v${version}");    // expect: Red v2
print("sum ${1 + 2} end");       // expect: sum 3 end
print("nested ${"in" + "ner"}"); // expect: nested inner
// A literal dollar sign needs no escape unless a brace follows it.
print("cost: $5");     // expect: cost: $5
print("brace: \${x}"); // expect: brace: ${x}

const text = "  Hello, World  ";
print(text.trim());               // expect: Hello, World
print(text.trim().upper());       // expect: HELLO, WORLD
print(text.trim().lower());       // expect: hello, world
print("a,b,c".split(","));        // expect: ["a", "b", "c"]
print("abc".split(""));           // expect: ["a", "b", "c"]
print("hello".len());             // expect: 5
print("hello".find("ll"));        // expect: 2
print("hello".find("zz"));        // expect: -1
print("hello".contains("ell"));   // expect: true
print("hello".starts_with("he")); // expect: true
print("hello".ends_with("lo"));   // expect: true
print("hello".sub(1, 3));         // expect: el
print("hello".sub(-2));           // expect: lo
print("a-b-a".replace("a", "X")); // expect: X-b-X
print("ab".repeat(3));            // expect: ababab
print("hello"[0]);                // expect: h
print("hello"[-1]);               // expect: o

// Trimming one end at a time, for the cases where the other end matters.
print("[" + "  hi  ".trim() + "]");       // expect: [hi]
print("[" + "  hi  ".trim_start() + "]"); // expect: [hi  ]
print("[" + "  hi  ".trim_end() + "]");   // expect: [  hi]
print("[" + "\t x".trim_start() + "]");   // expect: [x]
print("[" + "x \t".trim_end() + "]");     // expect: [x]
print("[" + "".trim_start() + "]");       // expect: []
print("[" + "abc".trim_end() + "]");      // expect: [abc]
