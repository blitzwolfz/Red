// What a screen has to be able to do, and a screen that is not one.
//
//   import "andy/backend" as backend;
//
// A backend is the only part of andy that knows what it is driving.
// There are three: a terminal (andy/term), a window (andy/native), and
// the Headless one below, which draws into memory and reads events from
// a list.
//
// The interface is small on purpose. Everything a program builds — the
// widgets, the layout, the focus, the key bindings — is above this line
// and cannot tell which backend it is running on. That is what makes the
// Headless one useful: a test drives a real interface and then reads the
// screen as text.

import "andy/geom" as geom;
import "andy/canvas" as canvas;
import "andy/event" as event;
import "andy/color" as color;

// The methods a backend provides. Subclassing this is not required —
// anything with these methods will do — but it documents the contract
// and gives the sensible answers to the questions most backends do not
// care about.
class Backend {
  init(name) {
    this.name = name;
    this.running = false;
  }

  // Take over the screen. After this, size() is meaningful and present()
  // may be called. Raises when the screen cannot be taken over, which is
  // how App knows to try a different backend.
  start() {
    this.running = true;
    return this;
  }

  // Give the screen back exactly as it was found. Must be safe to call
  // when start() failed or was never called, because the way a program
  // ends is not always the way it planned to.
  stop() {
    this.running = false;
    return this;
  }

  // The size of the drawing area, in cells.
  size() { return geom.Size(80, 24); }

  // Put a canvas on the screen. The backend is free to draw only what
  // changed, and all three of andy's do.
  present(surface) { return this; }

  // Every event that has arrived since the last call, as an array. Never
  // waits: an empty array means nothing has happened, and the event loop
  // decides what to do about that.
  poll() { return []; }

  // Whether the mouse is being reported. Widgets ask so that they can
  // say so — a list that cannot be clicked should not look clickable.
  has_mouse() { return false; }

  // How much colour the screen can show, as a color.Depth. None is the
  // safe answer for a screen that has not said: an interface drawn in
  // one colour is legible everywhere, and one drawn in sixteen on a
  // screen that has two is not.
  depth() { return color.Depth.None; }

  // Put text on the system clipboard. Not every backend can; the ones
  // that cannot say so, and a program can fall back to showing the text
  // and letting the user copy it themselves.
  set_clipboard(value) { return false; }

  // Ring the bell, flash the screen, or do nothing, whichever the screen
  // in question means by "the user has done something impossible".
  bell() { return this; }

  str() { return "<backend ${this.name}>"; }
}

// A backend that draws into memory and takes its events from a list.
//
//   const screen = backend.Headless(40, 12);
//   screen.send(event.Key("tab"));
//   app.run(screen);
//   print(screen.frame());
//
// This is how andy is tested, and it is also how an interface can be
// rendered into a string for a snapshot, a log, or a bug report from
// somebody whose terminal you do not have.
//
// It is a real backend, not a stub: the same widgets run, the same
// layout is computed and the same drawing happens. Only the last step,
// where cells become escape sequences, is replaced by keeping them.
class Headless < Backend {
  init(width = 80, height = 24) {
    super.init("headless");
    this.width = width;
    this.height = height;
    this.queue = [];
    // Every frame that was presented, in order. A test that wants to
    // check what happened in the middle of a sequence reads these
    // rather than only the last one.
    this.frames = [];
    this.surface = nil;
    this.mouse = true;
    this.clipboard = "";
    this.bells = 0;
    // Cells are kept whole, styles and all, so there is no palette here
    // to reduce anything to.
    this.color_depth = color.Depth.TrueColor;
  }

  depth() { return this.color_depth; }

  size() { return geom.Size(this.width, this.height); }

  has_mouse() { return this.mouse; }

  // Queue events for the loop to find. They come back out in the order
  // they went in, one batch per poll, which is what a real backend does
  // and what makes a test's timing predictable.
  send(...events) {
    for (let one in events) { this.queue.push(one); }
    return this;
  }

  // Queue a key by name, which is most of what a test wants to say.
  //
  //   screen.key("tab").key("enter").key("q");
  key(name, ...modifiers) {
    let ctrl = false;
    let alt = false;
    let shift = false;
    for (let mod in modifiers) {
      if (mod == "ctrl") { ctrl = true; }
      if (mod == "alt") { alt = true; }
      if (mod == "shift") { shift = true; }
    }
    let typed = "";
    if (name.len() == 1 and !ctrl and !alt) { typed = name; }
    return this.send(event.Key(name, typed, ctrl, alt, shift));
  }

  // Queue the characters of a string as individual key presses, which is
  // what typing into a field looks like from a widget's side.
  type(value) {
    for (let character in value.chars()) {
      this.send(event.Key(character, character));
    }
    return this;
  }

  click(x, y, button = 1) {
    this.send(event.Mouse("press", x, y, button));
    this.send(event.Mouse("release", x, y, button));
    return this;
  }

  // Change the size, and say so, the way a window manager would.
  resize(width, height) {
    this.width = width;
    this.height = height;
    return this.send(event.Resize(width, height));
  }

  poll() {
    const batch = this.queue;
    this.queue = [];
    return batch;
  }

  present(surface) {
    this.surface = surface.snapshot();
    this.frames.push(this.surface);
    return this;
  }

  // The last frame as one string, ready to print or compare.
  frame() {
    if (this.surface == nil) { return ""; }
    return this.surface.str();
  }

  // The last frame as an array of lines, for a test that checks one row.
  lines() {
    if (this.surface == nil) { return []; }
    return this.surface.lines();
  }

  set_clipboard(value) {
    this.clipboard = value;
    return true;
  }

  bell() {
    this.bells += 1;
    return this;
  }
}
