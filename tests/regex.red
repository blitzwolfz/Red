// Regular expressions.
//
// The matcher follows every alternative at once rather than one at a
// time, so there is no pattern that takes exponentially long. The last
// section is the usual proof of that.

const word = regex("\\w+");
print(word.test("hello"));               // expect: true
print(word.test("!!!"));                 // expect: false
print(word.find("say hello")["text"]);   // expect: say
print(word.find("!!!") == nil);          // expect: true

// A match says where it was as well as what it was.
const found = word.find("  hello  ");
print(found["start"], found["end"]);     // expect: 2 7
print(found["text"]);                    // expect: hello
print(found["groups"]);                  // expect: []

// Groups come back in order, with nil for one that took no part.
const date = regex("(\\d{4})-(\\d{2})-(\\d{2})");
print(date.groups());                    // expect: 3
const parts = date.find("on 2024-02-29 it happened");
print(parts["text"]);                    // expect: 2024-02-29
print(parts["groups"].join("/"));        // expect: 2024/02/29
print(regex("(a)|(b)").find("b")["groups"][0] == nil);   // expect: true

// find_all walks the whole string.
print(word.find_all("a bb ccc").map(fun (m) { return m["text"]; }));
// expect: ["a", "bb", "ccc"]
print(word.find_all("").len());          // expect: 0
print(regex("x*").find_all("axb").len());  // expect: 4

// replace, with $1 for a group and $$ for a literal dollar.
print(regex("l+").replace("hello world", "L"));      // expect: heLo worLd
print(date.replace("2024-02-29", "$3/$2/$1"));       // expect: 29/02/2024
print(regex("a").replace("aaa", "b", 2));            // expect: bba
print(regex("a").replace("a", "$$"));                // expect: $
print(regex("z").replace("abc", "!"));               // expect: abc

// split keeps what a capture group caught, and drops the rest.
print(regex(",\\s*").split("a, b,c ,  d"));   // expect: ["a", "b", "c ", "d"]
print(regex("(-)").split("a-b"));             // expect: ["a", "-", "b"]
print(regex(",").split("no commas"));         // expect: ["no commas"]

// The operators.
print(regex("^ab?c$").test("ac"));       // expect: true
print(regex("a{2,4}").find("aaaaa")["text"]);        // expect: aaaa
print(regex("a{2,}").find("aaaaa")["text"]);         // expect: aaaaa
print(regex("a{3}").test("aa"));         // expect: false
print(regex("(?:foo|bar)baz").find("xbarbazy")["text"]);   // expect: barbaz
print(regex("<(.+?)>").find("<a><b>")["groups"][0]);       // expect: a
print(regex("<(.+)>").find("<a><b>")["groups"][0]);        // expect: a><b
print(regex("[^aeiou]+").find("beautiful")["text"]);       // expect: b
print(regex("[a-c-]+").find("ab-c")["text"]);              // expect: ab-c
print(regex("[]]").test("]"));           // expect: true
print(regex("\\bcat\\b").test("a cat here"));        // expect: true
print(regex("\\bcat\\b").test("concatenate"));       // expect: false
print(regex("\\Bcat").test("concat"));   // expect: true

// Flags.
print(regex("cat", "i").test("CAT"));    // expect: true
print(regex("cat").test("CAT"));         // expect: false
print(regex("[a-z]+", "i").find("ABC")["text"]);     // expect: ABC
print(regex("^b", "m").find_all("a\nb\nc").len());   // expect: 1
print(regex("^b").find_all("a\nb\nc").len());        // expect: 0
print(regex("a.c", "s").test("a\nc"));   // expect: true
print(regex("a.c").test("a\nc"));        // expect: false

// Text, not bytes: one dot is one character.
print(regex(".").find_all("héllo").len());           // expect: 5
print(regex("h.llo").test("héllo"));                 // expect: true
print(regex("[é-ü]").test("ñ"));                     // expect: true
print(regex("\\u00e9").test("café"));                // expect: true
print(regex("é+").find("caféé")["end"]);             // expect: 7

// A pattern reports itself.
print(date.pattern());                   // expect: (\d{4})-(\d{2})-(\d{2})
print(regex("a", "im").flags());         // expect: im

// A pattern that cannot be compiled says why rather than misbehaving.
try { regex("("); } catch (e: "regex") {
  print(e.message);                      // expect: Cannot compile /(/: expected ')'.
}
try { regex("*a"); } catch (e: "regex") {
  print(e.message);                      // expect: Cannot compile /*a/: nothing for '*' to repeat.
}
try { regex("a{1,2000}"); } catch (e: "regex") {
  print(e.message);
  // expect: Cannot compile /a{1,2000}/: a repetition count above 1000 is not supported.
}
try { regex("a", "x"); } catch (e: "regex") {
  print(e.message);
  // expect: Cannot compile /a/: unknown flag 'x', expected i, m or s.
}
try { regex("(?=a)"); } catch (e: "regex") {
  print(e.message);
  // expect: Cannot compile /(?=a)/: only (?: ) is supported; lookaround is not part of this engine.
}

// The pattern that a backtracking engine cannot finish. Forty a's and no
// b is 2^40 attempts for one of those; here it is forty steps.
let bait = "";
for (let i in range(0, 40)) { bait += "a"; }
print(regex("(a+)+b").test(bait));       // expect: false
print(regex("^(a|a)*$").test(bait + "!"));  // expect: false
