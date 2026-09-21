// Reading and summarising access log lines.
//
// This module is the part of logstat worth testing on its own: it turns
// text into values and knows nothing about files, tasks or printing.
// examples/logstat/test_parse.red is its test.

// One parsed line. Nothing here is optional, so a Request only exists
// when the whole line made sense.
class Request {
  init(method, path, status, bytes) {
    this.method = method;
    this.path = path;
    this.status = status;
    this.bytes = bytes;
  }
}

// Malformed input is normal in a log, so a bad line is reported by
// returning nil rather than by throwing. The caller counts them.
//
// The shape being read is the common access log line:
//
//   1.2.3.4 - - [10/Oct/2024:13:55:36 +0000] "GET /a HTTP/1.1" 200 2326
fun parse(line) {
  const open = line.find("\"");
  if (open < 0) { return nil; }
  const close = line.find("\" ");
  if (close <= open) { return nil; }

  const request = line.sub(open + 1, close).split(" ");
  if (request.len() < 2) { return nil; }

  const tail = line.sub(close + 2).trim().split(" ");
  if (tail.len() < 2) { return nil; }

  const status = num(tail[0]);
  if (status == nil) { return nil; }

  // A dash in the size column means the response had no body.
  let bytes = 0;
  if (tail[1] != "-") {
    bytes = num(tail[1]);
    if (bytes == nil) { return nil; }
  }

  return Request(request[0], request[1], status, bytes);
}

// Counts, kept so that two reports can be added together. That is what
// lets one task handle each file and the results be merged at the end.
class Report {
  init() {
    this.lines = 0;
    this.skipped = 0;
    this.bytes = 0;
    this.byStatus = {};
    this.byPath = {};
  }

  // Adds one line of text. Gives back the report, so calls can chain.
  add(line) {
    const request = parse(line);
    if (request == nil) {
      this.skipped += 1;
      return this;
    }
    this.lines += 1;
    this.bytes += request.bytes;
    this.byStatus.set(request.status,
                      this.byStatus.get(request.status, 0) + 1);
    this.byPath.set(request.path, this.byPath.get(request.path, 0) + 1);
    return this;
  }

  // Folds another report into this one.
  merge(other) {
    this.lines += other.lines;
    this.skipped += other.skipped;
    this.bytes += other.bytes;
    for (let [status, count] in other.byStatus.entries()) {
      this.byStatus.set(status, this.byStatus.get(status, 0) + count);
    }
    for (let [path, count] in other.byPath.entries()) {
      this.byPath.set(path, this.byPath.get(path, 0) + count);
    }
    return this;
  }

  // The `limit` busiest paths, most requests first. Ties are broken by
  // path so that two runs over the same data print the same table.
  busiest(limit) {
    const rows = this.byPath.entries();
    rows.sort(fun (a, b) {
      if (a[1] != b[1]) { return a[1] > b[1]; }
      return a[0] < b[0];
    });
    return rows.slice(0, min(limit, rows.len()));
  }

  // Status codes in numeric order.
  statuses() {
    const codes = this.byStatus.keys();
    codes.sort();
    return codes;
  }
}

// Reads a whole file into a report. Missing files are the caller's
// problem, so this raises rather than guessing.
fun readFile(path) {
  const text = read_file(path);
  if (text == nil) {
    throw error("cannot read '${path}'", path, "io");
  }
  const report = Report();
  for (let line in text.split("\n")) {
    if (line.trim() == "") { continue; }
    report.add(line);
  }
  return report;
}
