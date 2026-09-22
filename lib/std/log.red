// Logging.
//
//   import "std/log" as log;
//
//   log.info("listening on ${port}");
//   log.error("cannot open ${path}", {"path": path});
//
// One line per event, with a level and a timestamp. Two formats: one for
// a person watching a terminal, and one for a machine reading a file.
//
// The module-level functions use a default logger writing to standard
// error, which is where log output belongs: a program's output is its
// output, and its log is not.

import "std/time" as time;
import "json" as json;

const DEBUG = 10;
const INFO = 20;
const WARN = 30;
const ERROR = 40;
const OFF = 100;

const LEVEL_NAMES = {10: "debug", 20: "info", 30: "warn", 40: "error"};

fun level_name(level) { return LEVEL_NAMES.get(level, "log"); }

fun level_from_name(name) {
  const lowered = str(name).lower();
  for (let [value, text] in LEVEL_NAMES.entries()) {
    if (text == lowered) { return value; }
  }
  return INFO;
}

class Logger {
  // `sink` is given one finished line. The default writes to standard
  // error, so log output does not end up mixed into a program's own.
  init(level = INFO, sink = nil, format = "text") {
    this.level = level;
    this.format = format;
    this.fields = {};
    this.sink = sink;
    if (this.sink == nil) {
      this.sink = fun (line) { eprint(line); };
    }
  }

  // A logger that adds these fields to everything it writes. For giving
  // every line of one request the same request id without passing it to
  // every function that logs.
  with(fields) {
    const child = Logger(this.level, this.sink, this.format);
    for (let [key, value] in this.fields.entries()) { child.fields[key] = value; }
    for (let [key, value] in fields.entries()) { child.fields[key] = value; }
    return child;
  }

  enabled(level) { return level >= this.level; }

  log(level, message, fields = nil) {
    if (!this.enabled(level)) { return this; }
    let all = {};
    for (let [key, value] in this.fields.entries()) { all[key] = value; }
    if (fields != nil) {
      for (let [key, value] in fields.entries()) { all[key] = value; }
    }

    if (this.format == "json") {
      all["time"] = time.iso();
      all["level"] = level_name(level);
      all["message"] = str(message);
      this.sink(json.stringify(all));
      return this;
    }

    let line = "${time.iso()} ${level_name(level).upper().pad_right(5)} " +
               "${message}";
    for (let key in all.keys().sort()) {
      line += " ${key}=${json.stringify(all[key])}";
    }
    this.sink(line);
    return this;
  }

  debug(message, fields = nil) { return this.log(DEBUG, message, fields); }
  info(message, fields = nil) { return this.log(INFO, message, fields); }
  warn(message, fields = nil) { return this.log(WARN, message, fields); }
  error(message, fields = nil) { return this.log(ERROR, message, fields); }
}

// The one the module-level functions use. RED_LOG sets its level, so a
// program's logging can be turned up without editing it.
let default_logger = Logger(level_from_name(env("RED_LOG", "info")));

fun logger() { return default_logger; }

fun set_logger(replacement) {
  default_logger = replacement;
  return default_logger;
}

fun set_level(level) {
  default_logger.level = level;
  return default_logger;
}

// Writes JSON objects rather than lines meant for a person.
fun set_json(on = true) {
  if (on) { default_logger.format = "json"; } else { default_logger.format = "text"; }
  return default_logger;
}

fun debug(message, fields = nil) { return default_logger.debug(message, fields); }
fun info(message, fields = nil) { return default_logger.info(message, fields); }
fun warn(message, fields = nil) { return default_logger.warn(message, fields); }
fun error(message, fields = nil) { return default_logger.error(message, fields); }
