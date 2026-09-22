// Time: measuring it, formatting it, and waiting.
//
//   import "std/time" as time;
//
// `time()` gives seconds since the epoch as a number, and `date()` and
// `format_time()` take one apart. What is here is the layer above: a
// stopwatch, durations that print as durations, a deadline, and the two
// date formats a network program keeps needing.

const SECOND = 1;
const MINUTE = 60;
const HOUR = 3600;
const DAY = 86400;

fun now() { return time(); }

// Seconds, to whatever precision the clock has. Named separately from
// now() because code that measures an interval should say so.
fun monotonic() { return time(); }

// Measures how long something takes.
//
//   const watch = time.Stopwatch();
//   work();
//   print(watch.elapsed());
class Stopwatch {
  init() {
    this.started = time();
    this.stopped = nil;
  }

  // Seconds since it started, or the interval it was stopped after.
  elapsed() {
    if (this.stopped != nil) { return this.stopped - this.started; }
    return time() - this.started;
  }

  stop() {
    this.stopped = time();
    return this.elapsed();
  }

  reset() {
    this.started = time();
    this.stopped = nil;
    return this;
  }

  str() { return duration(this.elapsed()); }
}

// A moment to stop by.
//
//   const until = time.Deadline(5);
//   while (!until.passed()) { ... }
class Deadline {
  init(seconds) { this.at = time() + seconds; }

  passed() { return time() >= this.at; }
  // Seconds left, never below zero, so it can be handed straight to a
  // socket timeout without a check.
  remaining() {
    const left = this.at - time();
    if (left < 0) { return 0; }
    return left;
  }

  // Waits until it passes, or until `seconds` have gone, whichever is
  // sooner. Parks the task rather than spinning.
  wait(seconds = nil) {
    let left = this.remaining();
    if (seconds != nil and seconds < left) { left = seconds; }
    if (left > 0) { sleep(left); }
    return this.passed();
  }
}

// A number of seconds as something readable: "1.5s", "2m 30s", "3h 4m".
// Small intervals keep their precision, because that is what they are
// for; large ones drop it, because nobody wants "1h 2m 3.472s".
fun duration(seconds) {
  if (seconds < 0) { return "-" + duration(-seconds); }
  if (seconds < 0.001) { return "${round(seconds * 1000000)}us"; }
  if (seconds < 1) { return "${round(seconds * 1000)}ms"; }
  if (seconds < 60) { return "${round(seconds * 100) / 100}s"; }
  if (seconds < HOUR) {
    const minutes = int(seconds / 60);
    return "${minutes}m ${round(seconds - minutes * 60)}s";
  }
  if (seconds < DAY) {
    const hours = int(seconds / HOUR);
    return "${hours}h ${int((seconds - hours * HOUR) / 60)}m";
  }
  const days = int(seconds / DAY);
  return "${days}d ${int((seconds - days * DAY) / HOUR)}h";
}

// ISO 8601 in UTC: "2026-09-21T18:30:00Z". The format to write into a
// log or a JSON document, because it sorts as text in time order.
fun iso(seconds = nil) {
  let at = seconds;
  if (at == nil) { at = time(); }
  return format_time(at, "%Y-%m-%dT%H:%M:%SZ", true);
}

// The format HTTP wants in a Date header:
// "Sun, 21 Sep 2026 18:30:00 GMT". Always English and always UTC,
// whatever the machine's locale is, which is why the names are written
// out here rather than left to strftime.
const WEEKDAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];
const MONTHS = ["Jan", "Feb", "Mar", "Apr", "May", "Jun",
                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"];

fun http_date(seconds = nil) {
  let at = seconds;
  if (at == nil) { at = time(); }
  const parts = date(at, true);
  const day = str(parts["day"]).pad_left(2, "0");
  const hour = str(parts["hour"]).pad_left(2, "0");
  const minute = str(parts["minute"]).pad_left(2, "0");
  const second = str(parts["second"]).pad_left(2, "0");
  const weekday = WEEKDAYS[parts["weekday"]];
  const month = MONTHS[parts["month"] - 1];
  return "${weekday}, ${day} ${month} ${parts["year"]} " +
         "${hour}:${minute}:${second} GMT";
}

// Calls `body` and gives back [result, seconds].
fun timed(body) {
  const watch = Stopwatch();
  const result = body();
  return [result, watch.stop()];
}
