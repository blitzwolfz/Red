// Directories, and what a path is.
//
// Everything here happens inside one directory next to this file, which
// is removed again at the end, so the test leaves nothing behind.

const base = source_dir() + "/fs_test";

// mkdir makes parents too, and succeeds for a directory that is already
// there, because the caller wanted it to exist and it does.
print(mkdir(base + "/one/two"));          // expect: true
print(mkdir(base + "/one/two"));          // expect: true
print(is_dir(base + "/one/two"));         // expect: true
print(is_dir(base + "/one"));             // expect: true
print(is_file(base + "/one"));            // expect: false

write_file(base + "/one/b.txt", "hello");
write_file(base + "/one/a.txt", "hi");

// Sorted, without "." and "..", so two runs walk a directory the same
// way.
print(list_dir(base + "/one").join(","));  // expect: a.txt,b.txt,two
print(list_dir(base + "/one/two").len());  // expect: 0

print(is_file(base + "/one/a.txt"));      // expect: true
print(is_dir(base + "/one/a.txt"));       // expect: false
print(file_size(base + "/one/a.txt"));    // expect: 2
print(file_size(base + "/one/b.txt"));    // expect: 5
print(modified(base + "/one/a.txt") > 1600000000);   // expect: true

// A path that is not there answers nil rather than raising, so a missing
// file and an empty one are different.
print(list_dir(base + "/nowhere") == nil);   // expect: true
print(file_size(base + "/nowhere") == nil);  // expect: true
print(modified(base + "/nowhere") == nil);   // expect: true
print(is_dir(base + "/nowhere"));            // expect: false
print(is_file(base + "/nowhere"));           // expect: false

print(rename(base + "/one/a.txt", base + "/one/c.txt"));  // expect: true
print(list_dir(base + "/one").join(","));  // expect: b.txt,c.txt,two
print(read_file(base + "/one/c.txt"));     // expect: hi
print(rename(base + "/nowhere", base + "/elsewhere"));    // expect: false

// remove_dir needs the directory to be empty: deleting a tree is a
// decision a program makes one file at a time.
print(remove_dir(base + "/one"));         // expect: false
print(remove_dir(base + "/one/two"));     // expect: true

remove_file(base + "/one/b.txt");
remove_file(base + "/one/c.txt");
print(remove_dir(base + "/one"));         // expect: true
print(remove_dir(base));                  // expect: true
print(exists(base));                      // expect: false
