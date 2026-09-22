// What happened: a key, the mouse, a resize, a request to close.
//
//   import "andy/event" as event;
//
// Every backend produces these and every widget consumes them, which is
// the whole reason a program written for the terminal runs in a window
// without being rewritten. A backend's job is to turn whatever it is
// given — escape sequences, window system messages — into one of these.
//
// An event carries a `handled` flag. A widget that deals with an event
// sets it, and dispatch stops there. That is what keeps a key press from
// being acted on twice by a control and the window containing it.

// Which sort of event this is.
enum Kind { Key, Mouse, Resize, Paste, Focus, Close, Tick }

// A key press, named rather than numbered.
//
// `key` is the name: a single character for an ordinary key, or one of
// the names in KEYS below. `text` is what the key would type, and is
// empty for a key that types nothing, which is how a text field tells
// "a" from "left" without a table.
//
// The modifiers are separate booleans rather than a bitmask because that
// is how they are written at the point of use: `if (e.ctrl and e.key ==
// "c")`.
class Key {
  init(key, text = "", ctrl = false, alt = false, shift = false) {
    this.kind = Kind.Key;
    this.key = key;
    this.text = text;
    this.ctrl = ctrl;
    this.alt = alt;
    this.shift = shift;
    this.handled = false;
  }

  // Does this press match a description like "ctrl+s", "alt+enter",
  // "shift+tab" or plain "q"? Written this way, a key binding table is
  // readable and a program never manipulates a modifier by hand.
  //
  // Matching is case insensitive on the key name. "ctrl+S" and "ctrl+s"
  // are the same press, because a terminal cannot tell them apart.
  matches(description) {
    let wantCtrl = false;
    let wantAlt = false;
    let wantShift = false;
    let name = "";
    for (let part in description.lower().split("+")) {
      const piece = part.trim();
      if (piece == "ctrl" or piece == "control" or piece == "c") { wantCtrl = true; }
      else if (piece == "alt" or piece == "meta" or piece == "option") { wantAlt = true; }
      else if (piece == "shift") { wantShift = true; }
      else { name = piece; }
    }
    if (name == "") { return false; }
    if (this.ctrl != wantCtrl or this.alt != wantAlt) { return false; }
    // Shift is only asked about for named keys. An ordinary letter
    // carries its own case, and demanding shift as well would make
    // "shift+a" never match anything.
    if (this.key.len() > 1 and this.shift != wantShift) { return false; }
    return this.key.lower() == name;
  }

  // Is this a key that types something? A text field inserts exactly the
  // keys for which this is true and leaves the rest to its container.
  is_text() {
    return this.text != "" and !this.ctrl and !this.alt;
  }

  str() {
    let out = "";
    if (this.ctrl) { out += "ctrl+"; }
    if (this.alt) { out += "alt+"; }
    if (this.shift and this.key.len() > 1) { out += "shift+"; }
    return out + this.key;
  }
}

// The mouse. Coordinates are in cells and are absolute: the top left of
// the screen is 0, 0, and a widget converts to its own frame when it
// needs to, because dispatch has to compare against frames anyway.
//
// `action` is "press", "release", "move", "drag" or "wheel". `button` is
// 1 for the left, 2 for the middle and 3 for the right. `wheel` is -1 for
// up and 1 for down, and zero for anything that is not a wheel.
class Mouse {
  init(action, x, y, button = 0, wheel = 0, ctrl = false, alt = false,
       shift = false) {
    this.kind = Kind.Mouse;
    this.action = action;
    this.x = x;
    this.y = y;
    this.button = button;
    this.wheel = wheel;
    this.ctrl = ctrl;
    this.alt = alt;
    this.shift = shift;
    this.handled = false;
  }

  is_press() { return this.action == "press"; }
  is_release() { return this.action == "release"; }
  is_drag() { return this.action == "drag"; }
  is_wheel() { return this.action == "wheel"; }

  // The same event with its coordinates moved into a widget's frame.
  // Containers pass this down so that a child never has to know where it
  // ended up on the screen.
  relative_to(rect) {
    const moved = Mouse(this.action, this.x - rect.x, this.y - rect.y,
                        this.button, this.wheel, this.ctrl, this.alt,
                        this.shift);
    moved.handled = this.handled;
    return moved;
  }

  str() {
    return "${this.action}(${this.x}, ${this.y}) button ${this.button}";
  }
}

// The screen changed shape. Sizes are in cells.
class Resize {
  init(width, height) {
    this.kind = Kind.Resize;
    this.width = width;
    this.height = height;
    this.handled = false;
  }

  str() { return "resize ${this.width}x${this.height}"; }
}

// Text arriving all at once, because somebody pasted it. Worth telling
// apart from a very fast typist: a text field inserts a paste whole and
// does not run a key binding for every character in it, which is what
// stops a pasted "q" from quitting the program.
class Paste {
  init(text) {
    this.kind = Kind.Paste;
    this.text = text;
    this.handled = false;
  }

  str() { return "paste ${this.text.len()} bytes"; }
}

// The window gained or lost the user's attention. A program can dim what
// it is showing, or stop animating, when nobody is looking.
class Focus {
  init(focused) {
    this.kind = Kind.Focus;
    this.focused = focused;
    this.handled = false;
  }

  str() {
    if (this.focused) { return "focus in"; }
    return "focus out";
  }
}

// The window manager, or the user, asked the program to stop.
class Close {
  init() {
    this.kind = Kind.Close;
    this.handled = false;
  }

  str() { return "close"; }
}

// Nothing happened, and a frame's worth of time passed. Sent on every
// pass of the event loop, which is what drives animation and anything
// else that has to notice the clock.
class Tick {
  init(seconds) {
    this.kind = Kind.Tick;
    this.seconds = seconds;
    this.handled = false;
  }

  str() { return "tick"; }
}

// Every name a key can have beyond a single character. A backend uses
// these spellings and a program compares against them, so the set is
// part of the contract rather than an implementation detail.
const KEYS = [
  "enter", "escape", "tab", "backspace", "delete", "insert",
  "up", "down", "left", "right",
  "home", "end", "pageup", "pagedown",
  "space",
  "f1", "f2", "f3", "f4", "f5", "f6",
  "f7", "f8", "f9", "f10", "f11", "f12",
];

// Is `name` one of the named keys, as opposed to a character?
fun is_named(name) { return KEYS.contains(name); }
