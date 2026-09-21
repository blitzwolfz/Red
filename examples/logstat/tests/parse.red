// Tests for the parse module, in the shape `red test` expects.
//
//   red test examples/logstat/tests
//
// The module is tested and the program is not, because the module is
// where the decisions live. logstat.red only moves values between it and
// the terminal.
//
// The import is relative to this file, not to wherever the runner was
// started, which is why it reaches back out of tests/.

import "../parse.red" as parse;

const LINE =
"10.0.0.1 - - [10/Oct/2024:13:55:36 +0000] \"GET /index.html HTTP/1.1\" 200 2326";

const request = parse.parse(LINE);
print(request.method); // expect: GET
print(request.path);   // expect: /index.html
print(request.status); // expect: 200
print(request.bytes);  // expect: 2326

// A dash in the size column is a response with no body.
print(parse.parse(
    "1.2.3.4 - - [x] \"GET /gone HTTP/1.1\" 404 -").bytes); // expect: 0

// Anything that is not a log line is nil, not an error.
print(parse.parse("") == nil);                 // expect: true
print(parse.parse("nonsense") == nil);         // expect: true
print(parse.parse("a \"GET /x\" 200") == nil); // expect: true
print(parse.parse(
    "a - - [x] \"GET /x HTTP/1.1\" oops 12") == nil); // expect: true

// A report counts what it understood and what it did not.
const report = parse.Report();
report.add(LINE);
report.add(LINE);
report.add("not a log line");
print(report.lines);   // expect: 2
print(report.skipped); // expect: 1
print(report.bytes);   // expect: 4652

// Two reports add up, which is what lets one task handle each file.
const other = parse.Report();
other.add("1.2.3.4 - - [x] \"GET /other HTTP/1.1\" 500 10");
report.merge(other);
print(report.lines);                // expect: 3
print(report.statuses().join(",")); // expect: 200,500

// Ties in the busiest table break by path, so the output is stable.
const tied = parse.Report();
for (let path in ["/b", "/a", "/b", "/a", "/c"]) {
  tied.add("x - - [t] \"GET ${path} HTTP/1.1\" 200 1");
}
const rows = tied.busiest(2);
print("${rows[0][0]} ${rows[0][1]}"); // expect: /a 2
print("${rows[1][0]} ${rows[1][1]}"); // expect: /b 2

// Reading a file that is not there is an error with kind "io".
try {
  parse.readFile("no-such-file.log");
} catch (e: "io") {
  print(e.message); // expect: cannot read 'no-such-file.log'
  print(e.payload); // expect: no-such-file.log
}

const fromFile = parse.readFile(source_dir() + "/../sample.log");
print(fromFile.lines);   // expect: 9
print(fromFile.skipped); // expect: 1
print(fromFile.bytes);   // expect: 17511
