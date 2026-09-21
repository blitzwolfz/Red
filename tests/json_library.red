// The JSON library, which is written in Red and found on the library
// search path.
//
// It is not called json.red, because a file next to the importing one
// wins over the library search path, and a test called json.red would
// import itself.

import "json.red" as json;

// Reading.
const doc = json.parse("{\"b\": [1, 2.5, -3e2], \"a\": \"hi\", \"c\": {\"d\": true, \"e\": null}}");
print(doc["a"]);                  // expect: hi
print(doc["b"]);                  // expect: [1, 2.5, -300]
print(doc["c"]["d"]);             // expect: true
print(doc["c"]["e"] == nil);      // expect: true
print(type(doc), type(doc["b"])); // expect: map array

print(json.parse("[]"));         // expect: []
print(json.parse("{}"));         // expect: {}
print(json.parse("  true  "));   // expect: true
print(json.parse("0"));          // expect: 0
print(json.parse("-0.5e2"));     // expect: -50
print(json.parse("\"\"").len()); // expect: 0

// Escapes, including the pairs that carry a code point above the basic
// plane the way UTF-16 does.
print(json.parse("\"caf\\u00e9\""));     // expect: café
print(json.parse("\"\\ud83d\\ude00\"")); // expect: 😀
print(json.parse("\"a\\tb\"").bytes());  // expect: [97, 9, 98]
print(json.parse("\"a\\\\b\\/c\""));     // expect: a\b/c

// Writing. Keys come out sorted, so the same data always gives the same
// text and two outputs can be compared.
print(json.stringify(doc));
// expect: {"a":"hi","b":[1,2.5,-300],"c":{"d":true,"e":null}}
print(json.stringify([1, nil, true, "x"])); // expect: [1,null,true,"x"]
print(json.stringify([]));                  // expect: []
print(json.stringify({}));                  // expect: {}
print(json.stringify("tab\there"));         // expect: "tab\there"
print(json.stringify(chr(1)));              // expect: "\u0001"
print(json.stringify("héllo"));             // expect: "héllo"
print(json.stringify(nil));                 // expect: null

// Indented.
print(json.stringify({"a": [1, 2]}, 2));
// expect: {
// expect:   "a": [
// expect:     1,
// expect:     2
// expect:   ]
// expect: }

// Anything written can be read back as itself.
const round = {"n": [1, 2.5, -300], "s": "café", "t": true, "z": nil,
  "nested": {"deep": [[]]}};
print(json.stringify(json.parse(json.stringify(round))) == json.stringify(round));
// expect: true

// Bad input says where it gave up.
try {
  json.parse("{\"a\":}");
} catch (e: "json") {
  print(e.message); // expect: unexpected '}' at position 5
  print(e.payload); // expect: 5
}
try {
  json.parse("[1,2] extra");
} catch (e: "json") {
  print(e.message); // expect: unexpected 'e' at position 6
}
try {
  json.parse("");
} catch (e: "json") {
  print(e.message); // expect: unexpected end of input at position 0
}
try {
  json.parse("\"unterminated");
} catch (e: "json") {
  print(e.message); // expect: unterminated string at position 13
}

// And so does anything that has no JSON form.
try {
  json.stringify(1e400);
} catch (e: "json") {
  print(e.message); // expect: JSON has no way to write inf
}
try {
  json.stringify(fun () { return 1; });
} catch (e: "json") {
  print(e.message); // expect: JSON has no way to write a function
}
