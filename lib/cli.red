// Command line parsing.
//
//   import "cli.red" as cli;
//
//   const spec = cli.Spec("wordcount", "Counts words in files.");
//   spec.flag("verbose", "v", "name each file as it is read");
//   spec.option("min", "m", "1", "ignore words shorter than this");
//   spec.rest("file", "files to read");
//
//   const opts = spec.parse(args());
//   if (opts.flag("help")) { print(spec.usage()); exit(0); }
//   for (let path in opts.rest()) { ... }
//
// Understands `--name`, `--name=value`, `-n`, `-n value`, and `--` to
// stop parsing and treat everything after it as a positional argument.
// A `--help` flag is always present.
//
// A bad argument is thrown as an error with kind "usage", so a program
// can report it and exit rather than crash:
//
//   try {
//     const opts = spec.parse(args());
//   } catch (e: "usage") {
//     print(e.message);
//     print(spec.usage());
//     exit(64);
//   }

const KIND_FLAG = "flag";
const KIND_VALUE = "value";

class Entry {
  init(long, short, kind, fallback, help) {
    this.long = long;
    this.short = short;
    this.kind = kind;
    this.fallback = fallback;
    this.help = help;
  }
}

// What one parse produced.
class Options {
  init(values, rest) {
    this.values = values;
    this.positional = rest;
  }

  // A flag's state, as true or false.
  flag(name) { return this.values.get(name, false) == true; }

  // An option's value, as a string. Falls back to what the spec declared.
  option(name) { return this.values.get(name, nil); }

  // The same, as a number, or nil when it is not one.
  number(name) {
    const text = this.option(name);
    if (text == nil) { return nil; }
    return num(text);
  }

  // Everything that was not an option.
  rest() { return this.positional; }
}

class Spec {
  init(name, summary) {
    this.name = name;
    this.summary = summary;
    this.entries = [];
    this.byLong = {};
    this.byShort = {};
    this.restName = "";
    this.restHelp = "";
    this.flag("help", "h", "show this message");
  }

  add(entry) {
    this.entries.push(entry);
    this.byLong.set(entry.long, entry);
    if (entry.short != "") { this.byShort.set(entry.short, entry); }
    return this;
  }

  // A switch that is either present or not. Pass "" for no short form.
  flag(long, short, help) {
    return this.add(Entry(long, short, KIND_FLAG, false, help));
  }

  // An option that takes a value. `fallback` is what `option()` gives
  // when it was not passed; nil is a fine fallback.
  option(long, short, fallback, help) {
    return this.add(Entry(long, short, KIND_VALUE, fallback, help));
  }

  // Names the positional arguments, for the usage message.
  rest(name, help) {
    this.restName = name;
    this.restHelp = help;
    return this;
  }

  fail(message) { throw error(message, nil, "usage"); }

  // Parses an argument list, usually args().
  parse(argv) {
    const values = {};
    for (let entry in this.entries) {
      values.set(entry.long, entry.fallback);
    }

    const positional = [];
    let i = 0;
    let onlyPositional = false;

    while (i < argv.len()) {
      const argument = argv[i];
      i += 1;

      if (onlyPositional) {
        positional.push(argument);
        continue;
      }
      if (argument == "--") {
        onlyPositional = true;
        continue;
      }
      if (!argument.starts_with("-") or argument == "-") {
        positional.push(argument);
        continue;
      }

      let name = "";
      let inlineValue = nil;
      if (argument.starts_with("--")) {
        name = argument.sub(2);
        const equals = name.find("=");
        if (equals >= 0) {
          inlineValue = name.sub(equals + 1);
          name = name.sub(0, equals);
        }
        if (!this.byLong.has(name)) { this.fail("unknown option '--${name}'"); }
        name = this.byLong.get(name).long;
      } else {
        const short = argument.sub(1);
        if (!this.byShort.has(short)) { this.fail("unknown option '-${short}'"); }
        name = this.byShort.get(short).long;
      }

      const entry = this.byLong.get(name);
      if (entry.kind == KIND_FLAG) {
        if (inlineValue != nil) {
          this.fail("'--${name}' is a flag and takes no value");
        }
        values.set(name, true);
        continue;
      }

      if (inlineValue != nil) {
        values.set(name, inlineValue);
        continue;
      }
      if (i >= argv.len()) { this.fail("'${entry.long}' needs a value"); }
      values.set(name, argv[i]);
      i += 1;
    }

    return Options(values, positional);
  }

  // A usage message built from the spec, with the options in a column.
  usage() {
    const lines = [];
    let head = "Usage: ${this.name} [options]";
    if (this.restName != "") { head += " [${this.restName}...]"; }
    lines.push(head);
    if (this.summary != "") {
      lines.push("");
      lines.push(this.summary);
    }
    lines.push("");
    lines.push("Options:");

    // Two passes, so the descriptions line up whatever the longest name
    // turns out to be.
    const left = [];
    for (let entry in this.entries) {
      let text = "  ";
      if (entry.short != "") {
        text += "-${entry.short}, ";
      } else {
        text += "    ";
      }
      text += "--${entry.long}";
      if (entry.kind == KIND_VALUE) { text += " <value>"; }
      left.push(text);
    }

    let width = 0;
    for (let text in left) {
      if (text.len() > width) { width = text.len(); }
    }
    for (let i in range(0, this.entries.len())) {
      const entry = this.entries[i];
      let line = left[i].pad_right(width + 2) + entry.help;
      if (entry.kind == KIND_VALUE and entry.fallback != nil) {
        line += " (default ${entry.fallback})";
      }
      lines.push(line);
    }

    if (this.restName != "" and this.restHelp != "") {
      lines.push("");
      lines.push("  ${this.restName}...".pad_right(width + 2) + this.restHelp);
    }
    return lines.join("\n");
  }
}
