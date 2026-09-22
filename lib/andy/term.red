// The terminal backend: escape sequences out, key presses in.
//
//   import "andy/term" as term;
//
//   const screen = term.Terminal();
//   screen.start();
//
// Almost nobody calls this directly. `andy.run()` picks a backend and
// this is usually the one it picks.
//
// Three things are worth knowing about what happens here.
//
// **The screen is borrowed, not taken.** Starting switches to the
// terminal's alternate screen and stopping switches back, so the shell
// scrollback a program was launched from is exactly as it was left,
// including the command that started it. Everything else the backend
// turns on — mouse reporting, bracketed paste, focus reporting — is
// turned off again in the same order.
//
// **Only what changed is drawn.** present() compares the frame with the
// one before it and sends the runs that differ, with one cursor move and
// one colour change per run. A full redraw of a large terminal is tens
// of kilobytes; a blinking cursor in a text field is a dozen bytes.
//
// **Input is a state machine over bytes.** A key press arrives as
// anything from one byte to a dozen, and a fast typist or a paste can
// split a sequence across two reads, so whatever does not yet make sense
// is kept and prepended to the next read rather than thrown away.

import "andy/geom" as geom;
import "andy/event" as event;
import "andy/color" as color;
import "andy/style" as style;
import "andy/text" as text;
import "andy/canvas" as canvas;
import "andy/backend" as backend;

const Backend = backend.Backend;
const ESC = chr(27);
const Depth = color.Depth;

// ---- the native half ----

// The extension is not optional here, the way crc32's is: there is no
// way to put a terminal into raw mode from Red alone. `available()`
// answers whether it loaded, and App uses that to choose a backend
// rather than failing.
let native = nil;
let tried = false;

fun load_native() {
  if (tried) { return native; }
  tried = true;
  try {
    const lib = ffi_open("andy_ext.so");
    if (lib.sym("andy_version")() != "1") { return nil; }
    native = {
      "open": lib.sym("andy_term_open"),
      "close": lib.sym("andy_term_close"),
      "is_tty": lib.sym("andy_term_is_tty"),
      "size": lib.sym("andy_term_size"),
      "resized": lib.sym("andy_term_resized"),
      "write": lib.sym("andy_term_write"),
      "read": lib.sym("andy_term_read"),
    };
  } catch (e: "ffi") {
    native = nil;
  }
  return native;
}

// Can this program drive a terminal at all? False when the extension is
// not built, and false when there is no terminal on the other end.
fun available() {
  const lib = load_native();
  if (lib == nil) { return false; }
  return lib["is_tty"]();
}

// ---- what the terminal can do ----

// How much colour to use, worked out from the environment.
//
// There is no reliable way to ask a terminal what it supports, so this
// is the usual set of guesses: COLORTERM is set by terminals that mean
// it, TERM names a terminfo entry whose name says how many colours it
// has, and NO_COLOR is a convention a user can set to mean "none".
fun detect_depth() {
  if (env("NO_COLOR") != nil) { return Depth.None; }
  const forced = env("ANDY_COLOR");
  if (forced != nil) {
    switch (forced.lower()) {
      case "none", "0": return Depth.None;
      case "16": return Depth.Ansi16;
      case "256": return Depth.Indexed256;
      case "true", "24", "truecolor": return Depth.TrueColor;
    }
  }
  const colorterm = env("COLORTERM", "").lower();
  if (colorterm == "truecolor" or colorterm == "24bit") {
    return Depth.TrueColor;
  }
  const term = env("TERM", "").lower();
  if (term == "" or term == "dumb") { return Depth.None; }
  if (term.contains("direct")) { return Depth.TrueColor; }
  if (term.contains("256")) { return Depth.Indexed256; }
  return Depth.Ansi16;
}

// ---- turning styles into escape sequences ----

// The SGR parameters for one colour, as a string, or "" for the default.
// `ground` is 3 for a foreground and 4 for a background, which is the
// only difference between the two.
fun color_params(value, ground, depth) {
  if (value.is_default() or depth == Depth.None) { return ""; }
  if (depth == Depth.TrueColor) {
    const parts = value.parts();
    return ";${ground}8;2;${parts[0]};${parts[1]};${parts[2]}";
  }
  if (depth == Depth.Indexed256) {
    let index = value.index;
    if (value.kind == "rgb") { index = color.nearest_256(value); }
    return ";${ground}8;5;${index}";
  }
  // Sixteen colours: the first eight are 30-37 and 40-47, and the bright
  // eight are 90-97 and 100-107.
  let index = value.index;
  if (value.kind == "rgb") { index = color.nearest_16(value); }
  if (index < 8) { return ";${ground * 10 + index}"; }
  if (ground == 3) { return ";${90 + index - 8}"; }
  return ";${100 + index - 8}";
}

// The whole escape sequence that puts the terminal into `want`. Always
// starts from a reset, because working out the difference from the
// previous style costs more than the handful of bytes it saves and gets
// subtly wrong whenever a terminal disagrees about which attributes an
// SGR 0 clears.
fun style_sequence(want, depth) {
  let params = "0";
  if (want.has(style.BOLD)) { params += ";1"; }
  if (want.has(style.DIM)) { params += ";2"; }
  if (want.has(style.ITALIC)) { params += ";3"; }
  if (want.has(style.UNDERLINE)) { params += ";4"; }
  if (want.has(style.BLINK)) { params += ";5"; }
  if (want.has(style.REVERSE)) { params += ";7"; }
  if (want.has(style.STRIKE)) { params += ";9"; }
  params += color_params(want.fg, 3, depth);
  params += color_params(want.bg, 4, depth);
  return ESC + "[" + params + "m";
}

// ---- reading what the user typed ----

// The names the C0 control codes stand for. Everything not here is
// ctrl and a letter, which is worked out rather than listed.
const CONTROL_KEYS = {
  9: "tab",
  13: "enter",
  10: "enter",
  27: "escape",
  127: "backspace",
  8: "backspace",
  32: "space",
};

// The numbers a CSI sequence ending in `~` can carry.
const TILDE_KEYS = {
  1: "home", 2: "insert", 3: "delete", 4: "end", 5: "pageup", 6: "pagedown",
  7: "home", 8: "end",
  11: "f1", 12: "f2", 13: "f3", 14: "f4", 15: "f5", 17: "f6", 18: "f7",
  19: "f8", 20: "f9", 21: "f10", 23: "f11", 24: "f12",
};

// The letters a CSI sequence can end in, for the keys that have one.
const LETTER_KEYS = {
  "A": "up", "B": "down", "C": "right", "D": "left",
  "H": "home", "F": "end", "E": "space",
  "P": "f1", "Q": "f2", "R": "f3", "S": "f4",
  "Z": "tab",
};

// A modifier parameter is one more than the sum of the bits: 1 for
// shift, 2 for alt, 4 for control. So 5 is control, 6 is control and
// shift, and 1 on its own is no modifiers at all.
fun modifiers_from(value) {
  if (value == nil or value < 1) { return [false, false, false]; }
  const bits = value - 1;
  return [bits & 4 != 0, bits & 2 != 0, bits & 1 != 0];
}

// Turns a buffer of bytes into events, and gives back what it could not
// make sense of yet.
//
//   const [events, rest] = term.decode(buffer, false);
//
// `pasting` says whether a bracketed paste is in progress, and comes
// back updated, because a paste's text arrives as ordinary bytes between
// two markers and must not be read as key presses.
//
// `flush` decides what a lone escape at the end of the buffer means. The
// escape key and the first byte of every escape sequence are the same
// byte, so a buffer ending in one is genuinely ambiguous: it is the
// escape key, unless the rest of a sequence is still on its way. With
// `flush` false it is left in the buffer, which is what the backend
// wants — it waits one more poll and then decides. With `flush` true it
// is the escape key, which is what a caller with the whole input in hand
// wants.
//
// This is a plain function rather than a method so that it can be tested
// without a terminal, which is most of how the escape sequence handling
// below is known to be right.
fun decode(buffer, pasting = false, pasted = "", flush = true) {
  const events = [];
  let i = 0;
  let paste_on = pasting;
  let paste_text = pasted;

  while (i < buffer.len()) {
    const code = buffer.code_at(i);

    if (paste_on) {
      // Everything up to the end marker is text, however it looks.
      const end = buffer.find(ESC + "[201~");
      if (end < 0 or end < i) {
        paste_text += buffer.sub(i);
        i = buffer.len();
        break;
      }
      paste_text += buffer.sub(i, end);
      events.push(event.Paste(paste_text));
      paste_text = "";
      paste_on = false;
      i = end + 6;
      continue;
    }

    if (code != 27) {
      const [made, used] = decode_plain(buffer, i);
      if (used == 0) { break; }
      if (made != nil) { events.push(made); }
      i += used;
      continue;
    }

    // An escape with nothing after it is the escape key. With something
    // after it, it may be a sequence — or it may be alt and a key, which
    // is spelled exactly the same way. A sequence that is still arriving
    // is left in the buffer for the next read, which is what makes a key
    // split across two reads still work.
    if (i + 1 >= buffer.len()) {
      if (!flush) { break; }
      events.push(event.Key("escape"));
      i += 1;
      continue;
    }

    const next = buffer[i + 1];
    if (next == "[") {
      const [made, used, started] = decode_csi(buffer, i);
      if (used == 0) { break; }
      if (started) { paste_on = true; }
      if (made != nil) { events.push(made); }
      i += used;
      continue;
    }
    if (next == "O") {
      // The application keypad form: ESC O and one letter. Terminals in
      // this mode send it for the arrow and function keys.
      if (i + 2 >= buffer.len()) { break; }
      const letter = buffer[i + 2];
      const name = LETTER_KEYS.get(letter, nil);
      if (name != nil) { events.push(event.Key(name)); }
      i += 3;
      continue;
    }
    if (next == ESC) {
      // Two escapes running: the first is the key.
      events.push(event.Key("escape"));
      i += 1;
      continue;
    }

    // Alt and whatever follows.
    const [made, used] = decode_plain(buffer, i + 1);
    if (used == 0) { break; }
    if (made != nil) {
      events.push(event.Key(made.key, "", made.ctrl, true, made.shift));
    }
    i += 1 + used;
  }

  return [events, buffer.sub(i), paste_on, paste_text];
}

// One key that is not part of an escape sequence: a control code, or a
// character, which may be several bytes of UTF-8. Gives the event and
// how many bytes it took, or zero bytes when the character is still
// arriving.
fun decode_plain(buffer, i) {
  const code = buffer.code_at(i);

  const named = CONTROL_KEYS.get(code, nil);
  if (named != nil) {
    let typed = "";
    if (named == "space") { typed = " "; }
    return [event.Key(named, typed), 1];
  }
  if (code < 27) {
    // Control and a letter: 1 is ctrl+a, 26 is ctrl+z. The four codes
    // above that are ctrl and a punctuation key, which no terminal
    // agrees about, so they are left alone.
    const letter = char(code + 96);
    return [event.Key(letter, "", true), 1];
  }
  if (code < 32) { return [nil, 1]; }

  // A character. UTF-8 says how many bytes it is from the first one; a
  // character that is not all here yet waits for the next read.
  let length = 1;
  if (code >= 0xf0) { length = 4; }
  else if (code >= 0xe0) { length = 3; }
  else if (code >= 0xc0) { length = 2; }
  if (i + length > buffer.len()) { return [nil, 0]; }
  const character = buffer.sub(i, i + length);
  return [event.Key(character, character), length];
}

// A CSI sequence: ESC [ then parameters then a final byte. Gives the
// event, how many bytes it took, and whether it opened a paste.
fun decode_csi(buffer, start) {
  let i = start + 2;
  let private_marker = "";
  if (i < buffer.len() and (buffer[i] == "<" or buffer[i] == "?" or
                            buffer[i] == ">")) {
    private_marker = buffer[i];
    i += 1;
  }
  let params = "";
  while (i < buffer.len()) {
    const c = buffer[i];
    const code = buffer.code_at(i);
    if ((code >= 48 and code <= 57) or c == ";" or c == ":") {
      params += c;
      i += 1;
      continue;
    }
    break;
  }
  if (i >= buffer.len()) { return [nil, 0, false]; }
  const final = buffer[i];
  const length = i + 1 - start;
  const numbers = split_params(params);

  // The mouse, in the extended form every terminal worth using speaks.
  if (private_marker == "<" and (final == "M" or final == "m")) {
    return [mouse_event(numbers, final == "M"), length, false];
  }

  if (final == "~") {
    const first = number_at(numbers, 0, 0);
    if (first == 200) { return [nil, length, true]; }
    if (first == 201) { return [nil, length, false]; }
    const name = TILDE_KEYS.get(first, nil);
    if (name == nil) { return [nil, length, false]; }
    const [ctrl, alt, shift] = modifiers_from(number_at(numbers, 1, 1));
    return [event.Key(name, "", ctrl, alt, shift), length, false];
  }

  if (final == "I") { return [event.Focus(true), length, false]; }
  if (final == "O") { return [event.Focus(false), length, false]; }

  const name = LETTER_KEYS.get(final, nil);
  if (name == nil) { return [nil, length, false]; }
  let [ctrl, alt, shift] = modifiers_from(number_at(numbers, 1, 1));
  // Shift and tab has a sequence of its own and carries no modifier
  // parameter, so it is filled in here.
  if (final == "Z") { shift = true; }
  return [event.Key(name, "", ctrl, alt, shift), length, false];
}

fun split_params(params) {
  if (params == "") { return []; }
  const out = [];
  for (let piece in params.split(";")) {
    // A colon separates sub parameters, which nothing here uses.
    const head = piece.split(":")[0];
    const value = num(head);
    if (value == nil) { out.push(nil); } else { out.push(floor(value)); }
  }
  return out;
}

fun number_at(numbers, index, fallback) {
  if (index >= numbers.len()) { return fallback; }
  if (numbers[index] == nil) { return fallback; }
  return numbers[index];
}

// The mouse report: a button code, a column and a row, all one based,
// and M for a press and m for a release.
//
// The button code carries the modifiers and two flags: bit 5 means the
// mouse moved rather than being clicked, and bit 6 means the wheel.
fun mouse_event(numbers, pressed) {
  const code = number_at(numbers, 0, 0);
  const x = number_at(numbers, 1, 1) - 1;
  const y = number_at(numbers, 2, 1) - 1;
  const shift = code & 4 != 0;
  const alt = code & 8 != 0;
  const ctrl = code & 16 != 0;
  const moving = code & 32 != 0;
  const wheeling = code & 64 != 0;
  const button = code & 3;

  if (wheeling) {
    let direction = -1;
    if (button == 1) { direction = 1; }
    return event.Mouse("wheel", x, y, 0, direction, ctrl, alt, shift);
  }
  if (moving) {
    // Button 3 while moving means no button is down: the pointer is
    // being moved rather than dragged.
    if (button == 3) {
      return event.Mouse("move", x, y, 0, 0, ctrl, alt, shift);
    }
    return event.Mouse("drag", x, y, button + 1, 0, ctrl, alt, shift);
  }
  if (!pressed) {
    return event.Mouse("release", x, y, button + 1, 0, ctrl, alt, shift);
  }
  return event.Mouse("press", x, y, button + 1, 0, ctrl, alt, shift);
}

// The bytes that turn `previous` into `surface` on a terminal of this
// colour depth, or "" when there is nothing to do.
//
// A plain function rather than a method, so that what andy would send
// can be examined without a terminal to send it to — the same reason
// decode() above is one.
fun frame_bytes(surface, previous, depth) {
  const runs = surface.diff(previous);
  if (runs.len() == 0 and previous != nil) { return ""; }

  const parts = [ESC + "[?25l"];
  if (previous == nil) {
    // Nothing to compare against, which means this is the first frame or
    // the terminal has just changed shape. Erase before drawing.
    //
    // A frame only covers the canvas, and after a terminal shrinks the
    // canvas is smaller than the screen: without this, whatever the old
    // wider frame left in the columns to the right of the new one stays
    // there, because nothing will ever write over it. The reset comes
    // first so that the erase uses the terminal's own background rather
    // than whatever colour the last run happened to leave behind.
    parts.push(ESC + "[0m" + ESC + "[2J");
  }

  // One cursor move per run, and a colour change only when the colour
  // actually changes, which for a row of one style is a single sequence
  // for the whole row.
  let last = nil;
  for (let [x, y, run_style, cells] in runs) {
    parts.push(ESC + "[" + str(y + 1) + ";" + str(x + 1) + "H");
    if (last == nil or !(last == run_style)) {
      parts.push(style_sequence(run_style, depth));
      last = run_style;
    }
    parts.push(cells.join(""));
  }
  parts.push(ESC + "[0m");
  return parts.join("");
}

// ---- the backend itself ----

class Terminal < Backend {
  init(options = nil) {
    super.init("terminal");
    if (options == nil) { options = {}; }
    this.mouse = options.get("mouse", true);
    this.alternate = options.get("alternate", true);
    this.paste = options.get("paste", true);
    this.title = options.get("title", nil);
    this.color_depth = options.get("depth", nil);
    if (this.color_depth == nil) { this.color_depth = detect_depth(); }

    this.previous = nil;
    this.buffer = "";
    this.pasting = false;
    this.pasted = "";
    // How many polls a lone escape has been sitting in the buffer. One
    // is enough: if the rest of a sequence were coming it would already
    // be here, since a terminal writes a sequence in one go.
    this.waited = 0;
    this.cursor_shown = false;
    this.cached = geom.Size(80, 24);
    this.lib = nil;
    // True while the size above is a guess rather than something the
    // terminal said. See measure().
    this.guessed_size = false;
  }

  depth() { return this.color_depth; }
  has_mouse() { return this.mouse; }

  start() {
    this.lib = load_native();
    if (this.lib == nil) {
      throw error("the andy terminal extension is not installed", nil, "ffi");
    }
    if (!this.lib["open"]()) {
      throw error("not a terminal", nil, "io");
    }
    this.running = true;
    this.cached = this.measure();

    let out = "";
    if (this.alternate) { out += ESC + "[?1049h"; }
    out += ESC + "[?25l";           // hide the cursor while we draw
    if (this.mouse) {
      out += ESC + "[?1000h";       // report clicks
      out += ESC + "[?1002h";       // and movement while a button is down
      out += ESC + "[?1006h";       // in the extended form, for wide screens
    }
    if (this.paste) { out += ESC + "[?2004h"; }
    out += ESC + "[?1004h";         // tell us when the window gains focus
    if (this.title != nil) {
      out += ESC + "]0;" + this.title + chr(7);
    }
    out += ESC + "[0m" + ESC + "[2J";
    this.write(out);
    this.previous = nil;
    return this;
  }

  // Everything start() turned on, turned off, and in the reverse order,
  // so that a terminal which only half understood us is left no worse
  // than it was found.
  stop() {
    if (!this.running) { return this; }
    this.running = false;
    let out = ESC + "[0m";
    out += ESC + "[?1004l";
    if (this.paste) { out += ESC + "[?2004l"; }
    if (this.mouse) {
      out += ESC + "[?1006l" + ESC + "[?1002l" + ESC + "[?1000l";
    }
    out += ESC + "[?25h";
    if (this.alternate) { out += ESC + "[?1049l"; }
    this.write(out);
    if (this.lib != nil) { this.lib["close"](); }
    this.previous = nil;
    return this;
  }

  write(value) {
    if (this.lib == nil) { return this; }
    this.lib["write"](value);
    return this;
  }

  measure() {
    this.guessed_size = false;
    if (this.lib != nil) {
      const reported = this.lib["size"]();
      if (reported != nil) {
        const parts = reported.split(",");
        const w = num(parts[0]);
        const h = num(parts[1]);
        if (w != nil and h != nil and w > 0 and h > 0) {
          return geom.Size(floor(w), floor(h));
        }
      }
    }
    // A terminal that will not say how big it is. The environment
    // sometimes knows, and 80x24 is what everything assumed before any
    // of this existed.
    //
    // Either way the answer is a guess, which poll() remembers: a
    // terminal that could not be measured sends no signal when the
    // guess turns out to be wrong, so the only way to find out is to
    // keep asking until it answers.
    this.guessed_size = true;
    const columns = num(env("COLUMNS", "80"));
    const rows = num(env("LINES", "24"));
    if (columns == nil or rows == nil) { return geom.Size(80, 24); }
    return geom.Size(floor(columns), floor(rows));
  }

  size() { return this.cached; }

  // Draws the runs that differ from the last frame.
  //
  // The cursor is hidden for the duration and put back afterwards, in
  // one write, so that it is never seen skipping across the screen.
  present(surface) {
    if (!this.running) { return this; }
    if (surface.width != this.cached.width or
        surface.height != this.cached.height) {
      // The canvas and the terminal disagree about the size, which
      // happens for one frame after a resize. Start the comparison
      // again rather than write the difference between two shapes.
      this.previous = nil;
    }
    const bytes = frame_bytes(surface, this.previous, this.color_depth);
    if (bytes == "") {
      this.place_cursor(surface);
      return this;
    }
    this.write(bytes);
    this.previous = surface.snapshot();
    this.place_cursor(surface);
    return this;
  }

  place_cursor(surface) {
    if (surface.cursor == nil) {
      if (this.cursor_shown) {
        this.write(ESC + "[?25l");
        this.cursor_shown = false;
      }
      return this;
    }
    const at = ESC + "[" + str(surface.cursor.y + 1) + ";" +
               str(surface.cursor.x + 1) + "H";
    if (this.cursor_shown) {
      this.write(at);
    } else {
      this.write(at + ESC + "[?25h");
      this.cursor_shown = true;
    }
    return this;
  }

  poll() {
    if (!this.running) { return []; }
    const events = [];

    // A resize is noticed by a signal handler in the extension and
    // reported here, because the terminal does not send one.
    //
    // While the size is only a guess there is no signal to wait for, so
    // it is asked for again every pass until the terminal answers.
    if (this.lib["resized"]() or this.guessed_size) {
      const now = this.measure();
      if (now.width != this.cached.width or now.height != this.cached.height) {
        this.cached = now;
        this.previous = nil;
        events.push(event.Resize(now.width, now.height));
      }
    }

    const arrived = this.lib["read"](0);
    if (arrived == "") {
      // Nothing new. A lone escape left over from last time is the
      // escape key after all: the rest of a sequence would have arrived
      // in the same write as its first byte.
      if (this.buffer == ESC and !this.pasting) {
        this.waited += 1;
        if (this.waited > 1) {
          this.buffer = "";
          this.waited = 0;
          events.push(event.Key("escape"));
        }
      }
      return events;
    }
    this.waited = 0;
    this.buffer += arrived;
    const [decoded, rest, pasting, pasted] =
        decode(this.buffer, this.pasting, this.pasted, false);
    this.buffer = rest;
    this.pasting = pasting;
    this.pasted = pasted;
    for (let one in decoded) { events.push(one); }
    return events;
  }

  // Copies through the terminal itself, with OSC 52. Not every terminal
  // does this, and some ask the user first, so there is no way to know
  // whether it worked; it is reported as sent rather than as done.
  set_clipboard(value) {
    this.write(ESC + "]52;c;" + base64(value) + chr(7));
    return true;
  }

  bell() {
    this.write(chr(7));
    return this;
  }
}

// OSC 52 carries its payload in base64. Small enough to write here
// rather than reach for something.
const BASE64_ALPHABET =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

fun base64(value) {
  const bytes = value.bytes();
  const out = [];
  let i = 0;
  while (i + 2 < bytes.len()) {
    const n = bytes[i] * 65536 + bytes[i + 1] * 256 + bytes[i + 2];
    out.push(BASE64_ALPHABET[floor(n / 262144) % 64]);
    out.push(BASE64_ALPHABET[floor(n / 4096) % 64]);
    out.push(BASE64_ALPHABET[floor(n / 64) % 64]);
    out.push(BASE64_ALPHABET[n % 64]);
    i += 3;
  }
  const left = bytes.len() - i;
  if (left == 1) {
    const n = bytes[i] * 16;
    out.push(BASE64_ALPHABET[floor(n / 64) % 64]);
    out.push(BASE64_ALPHABET[n % 64]);
    out.push("=");
    out.push("=");
  } else if (left == 2) {
    const n = bytes[i] * 1024 + bytes[i + 1] * 4;
    out.push(BASE64_ALPHABET[floor(n / 4096) % 64]);
    out.push(BASE64_ALPHABET[floor(n / 64) % 64]);
    out.push(BASE64_ALPHABET[n % 64]);
    out.push("=");
  }
  return out.join("");
}
