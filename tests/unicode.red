// Text, as opposed to bytes.
//
// A Red string is a sequence of bytes and may hold anything. len(),
// indexing and code_at() work in bytes. chars(), code_points(),
// char_len() and for-in work in characters, which is what makes text
// that is not ASCII usable.

const text = "héllo → 世界";

print(text);                  // expect: héllo → 世界
print(text.len());            // expect: 17
print(text.char_len());       // expect: 10
print(text.chars().len());    // expect: 10
print(text.chars()[1]);       // expect: é
print(text.code_points()[1]); // expect: 233
print(text.code_points()[8]); // expect: 19990

// Taking a string apart by characters and putting it back gives exactly
// what was there.
print(text.chars().join("") == text); // expect: true

// for-in walks characters too.
const walked = [];
for (let c in text) { walked.push(c); }
print(walked.len());            // expect: 10
print(walked.join("") == text); // expect: true

// A set of characters, not of bytes.
print(set("héllo").len());                     // expect: 4
print(set("banana").items().sort().join(",")); // expect: a,b,n

// char() is to characters what chr() is to bytes.
print(char(233), char(8594), char(65)); // expect: é → A
print(char(128512));                    // expect: 😀
print(char(128512).len());              // expect: 4
print(char(128512).char_len());         // expect: 1
print(chr(65) == char(65));             // expect: true

// Escapes: four hex digits, or braces for anything wider.
print("Aé€");                       // expect: Aé€
print("\u{41}\u{e9}\u{1f600}");     // expect: Aé😀
print("\u{1F600}" == char(128512)); // expect: true

// Bytes are still bytes when that is what is wanted.
print("A".bytes());       // expect: [65]
print(char(233).bytes()); // expect: [195, 169]
print(text.code_at(0));   // expect: 104

// Data that is not valid UTF-8 survives being walked. Every byte that
// does not begin a well formed sequence comes back on its own.
let raw = "";
for (let code in [200, 65, 228, 184, 0, 255]) { raw += chr(code); }
print(raw.len());                   // expect: 6
print(raw.chars().len());           // expect: 6
print(raw.chars().join("") == raw); // expect: true

// Every byte value round trips through chars().
let every = "";
for (let code in range(0, 256)) { every += chr(code); }
print(every.chars().join("") == every); // expect: true

// Case conversion covers the whole of Unicode: the simple one-to-one
// mappings, and the handful that change length.
print("héllo".upper());   // expect: HÉLLO
print("HÉLLO".lower());   // expect: héllo
print("αθηνα".upper());   // expect: ΑΘΗΝΑ
print("ПРИВЕТ".lower());  // expect: привет
print("straße".upper());  // expect: STRASSE
print("ﬁle".upper());     // expect: FILE
print("abc123!".upper()); // expect: ABC123!

// Data that is not text is copied through rather than mangled.
let mixed = "";
for (let code in [200, 97, 255]) { mixed += chr(code); }
print(mixed.upper().bytes()); // expect: [200, 65, 255]

// The regex ignore-case flag uses the same mappings.
print(regex("café", "i").test("CAFÉ"));           // expect: true
print(regex("абв", "i").test("АБВ"));             // expect: true
print(regex("[à-ÿ]+", "i").find("xÉÈy")["text"]); // expect: ÉÈ

// Except where a mapping changes the length, which a matcher working one
// character at a time cannot represent.
print(regex("straße", "i").test("STRASSE")); // expect: false
