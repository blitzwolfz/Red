// `red build` writes a program and everything it imports into a copy of
// the interpreter, so the result runs on a machine with no Red on it.

const here = source_dir();
const work = "${here}/.build-test";
mkdir(work);

// A library, and a program that imports it. The library is not on the
// search path and not beside the program's eventual home, so nothing but
// the bundle can supply it once the sources are gone.
write_file("${work}/greeting.red", "fun greet(who) { return \"hello, \" + who; }\n");
write_file("${work}/app.red",
  "import \"greeting.red\" as greeting;\n" +
  "let who = \"world\";\n" +
  "if (args().len() > 0) { who = args()[0]; }\n" +
  "print(greeting.greet(who));\n");

const built = run([exe_path(), "build", "${work}/app.red", "-o", "${work}/app"]);
print(built["code"]);                      // expect: 0
print(built["out"].contains("2 modules")); // expect: true

// Remove the sources. What is left has to be enough.
remove_file("${work}/app.red");
remove_file("${work}/greeting.red");

const plain = run(["${work}/app"]);
print(plain["code"]);       // expect: 0
print(plain["out"].trim()); // expect: hello, world

const witharg = run(["${work}/app", "Red"]);
print(witharg["out"].trim()); // expect: hello, Red

// The interpreter it was copied from is unchanged: a bundle is only ever
// read from the file that has one.
const version = run([exe_path(), "version"]);
print(version["out"].starts_with("red ")); // expect: true

remove_file("${work}/app");
remove_dir(work);
