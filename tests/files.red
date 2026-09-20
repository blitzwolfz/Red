// File input and output. Everything happens in a temporary file that is
// removed at the end.

const path = "./red_file_test.tmp";

print(write_file(path, "first\nsecond\nthird\n"));   // expect: true
print(read_file(path).split("\n").len());            // expect: 4
print(append_file(path, "fourth\n"));                // expect: true

const handle = open(path, "r");
print(handle.is_open());                             // expect: true
print(handle.read_line());                           // expect: first
print(handle.read_line());                           // expect: second
handle.close();
print(handle.is_open());                             // expect: false

const all = open(path, "r");
print(all.lines());                                  // expect: ["first", "second", "third", "fourth"]
all.close();

const out = open(path, "w");
out.write("replaced");
out.close();
print(read_file(path));                              // expect: replaced

print(exists(path));                                 // expect: true
print(remove_file(path));                            // expect: true
print(exists(path));                                 // expect: false

// Reading a file that is not there gives nil rather than failing.
print(read_file("./definitely_not_here.tmp"));       // expect: nil

// Opening one does fail, with a message naming the path.
try {
  open("./definitely_not_here.tmp", "r");
} catch (e) {
  print(e.message.contains("Cannot open"));          // expect: true
}
