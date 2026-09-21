// logstat: a summary of one or more access logs.
//
//   red examples/logstat/logstat.red examples/logstat/sample.log
//   red examples/logstat/logstat.red --top 3 --json *.log
//
// A worked example of a whole program rather than one feature:
// arguments, modules, files, errors, tasks, output and exit codes.
// docs/guide.md builds it up a piece at a time and explains the choices.

import "cli.red" as cli;
import "parse.red" as parse;

const VERSION = "1.0.0";

// Exit codes, following the convention the interpreter itself uses: 64
// for a command line that made no sense, 74 for input that could not be
// read.
const EXIT_USAGE = 64;
const EXIT_INPUT = 74;

fun describe() {
  const spec = cli.Spec("logstat", "Summarises access logs.");
  spec.option("top", "t", "5", "how many paths to list");
  spec.flag("json", "j", "write a JSON object instead of a table");
  spec.flag("quiet", "q", "totals only, no path table");
  spec.flag("version", "", "print the version and stop");
  spec.rest("file", "log files to read, or none to read standard input");
  return spec;
}

// Reads every file, one task each, and folds the results together. The
// files are read in parallel but merged in the order they were named, so
// the output does not depend on which task finished first.
//
// An error raised inside a task comes back out of join() as itself, with
// its kind and payload, so the catch clause here is the same one that
// would have worked had readFile been called directly.
fun readAll(paths) {
  const tasks = [];
  for (let path in paths) { tasks.push(spawn parse.readFile(path)); }

  const total = parse.Report();
  const failures = [];
  for (let task in tasks) {
    try {
      total.merge(task.join());
    } catch (e: "io") {
      failures.push(e.message);
    }
  }
  return [total, failures];
}

// With no files named, the log arrives on standard input.
fun readStandardInput() {
  const report = parse.Report();
  for (;;) {
    const line = input();
    if (line == nil) { break; }
    if (line.trim() == "") { continue; }
    report.add(line);
  }
  return report;
}

fun printTable(report, top, quiet) {
  print("lines     ${report.lines}");
  print("skipped   ${report.skipped}");
  print("bytes     ${report.bytes}");

  print("");
  print("status  requests");
  for (let status in report.statuses()) {
    print(str(status).pad_right(8) + str(report.byStatus[status]));
  }

  if (quiet) { return; }

  const rows = report.busiest(top);
  if (rows.len() == 0) { return; }

  // The path column is as wide as the widest path, so nothing wraps and
  // nothing is padded further than it needs to be.
  let width = 4;
  for (let [path, count] in rows) {
    if (path.len() > width) { width = path.len(); }
  }
  print("");
  print("path".pad_right(width + 2) + "requests");
  for (let [path, count] in rows) {
    print(path.pad_right(width + 2) + str(count));
  }
}

// Enough JSON for this program's own output. A general writer would have
// to escape more than a path ever contains.
fun quote(text) {
  return "\"" + text.replace("\\", "\\\\").replace("\"", "\\\"") + "\"";
}

fun printJson(report, top) {
  const statuses = [];
  for (let status in report.statuses()) {
    statuses.push("${quote(str(status))}: ${report.byStatus[status]}");
  }
  const paths = [];
  for (let [path, count] in report.busiest(top)) {
    paths.push("[${quote(path)}, ${count}]");
  }
  print("{" +
    "\"lines\": ${report.lines}, " +
    "\"skipped\": ${report.skipped}, " +
    "\"bytes\": ${report.bytes}, " +
    "\"status\": {" + statuses.join(", ") + "}, " +
    "\"busiest\": [" + paths.join(", ") + "]}");
}

fun main() {
  const spec = describe();

  let options = nil;
  try {
    options = spec.parse(args());
  } catch (e: "usage") {
    // A bad command line is the user's mistake, not a crash. Say what was
    // wrong, show what was expected, and leave.
    print(e.message);
    print("");
    print(spec.usage());
    return EXIT_USAGE;
  }

  if (options.flag("help")) {
    print(spec.usage());
    return 0;
  }
  if (options.flag("version")) {
    print("logstat ${VERSION}");
    return 0;
  }

  const top = options.number("top");
  if (top == nil or top < 1) {
    print("--top needs a positive number, got '${options.option("top")}'");
    return EXIT_USAGE;
  }

  let report = nil;
  let failures = [];
  if (options.rest().len() == 0) {
    report = readStandardInput();
  } else {
    const [read, problems] = readAll(options.rest());
    report = read;
    failures = problems;
  }

  // Report what could not be read, but still print what could. A run over
  // twenty files should not lose nineteen results to one bad path.
  for (let message in failures) { print("logstat: " + message); }

  if (options.flag("json")) {
    printJson(report, top);
  } else {
    printTable(report, top, options.flag("quiet"));
  }

  if (failures.len() > 0) { return EXIT_INPUT; }
  return 0;
}

const status = main();
if (status != 0) { exit(status); }
