// The window backend: andy in a window of its own.
//
//   import "andy/native" as native;
//
//   const screen = native.Window({"title": "Notes"});
//
// Usually chosen rather than built: `andy.run(root, {"backend":
// "native"})` picks this, and `andy.run` on its own picks it when there
// is no terminal to draw on.
//
// The window is a program of its own, `andy-gui`, built beside the
// interpreter. This half starts it, hands it frames and reads back what
// the user did, over a socket on the loopback interface.
//
// Why a second process rather than an extension? Because every window
// system insists that its event loop runs on the process's main thread,
// and Red's scheduler moves a task between worker threads as it sees
// fit. There is no main thread available to be had inside the
// interpreter, so andy-gui brings its own. Two things fall out of that
// which are worth having anyway: a window that hangs cannot hang the
// program, and the socket parks the task while it waits, so a window
// costs no thread at all.
//
// The grid is the same grid the terminal draws. A program written for
// one runs in the other unchanged; what the window adds is a real
// window — its own size, its own title, resizing in whole cells, the
// system clipboard — and colour that never has to be reduced to a
// palette.

import "andy/geom" as geom;
import "andy/event" as event;
import "andy/color" as color;
import "andy/style" as style;
import "andy/canvas" as canvas;
import "andy/backend" as backend;

const Backend = backend.Backend;

// The version of the protocol below. andy-gui reports its own, and a
// mismatch is refused rather than acted on.
const PROTOCOL = "1";

// Where andy-gui is. It is built beside the interpreter, so the library
// search path finds it: `library_paths()` includes the interpreter's own
// directory, which is where the build puts it.
fun executable() {
  const named = env("ANDY_GUI");
  if (named != nil) { return named; }
  for (let directory in library_paths()) {
    const candidate = directory + "/andy-gui";
    if (is_file(candidate)) { return candidate; }
  }
  const found = which("andy-gui");
  if (found != nil) { return found; }
  return nil;
}

// Is there a window system to open a window on?
//
// Three things have to be true: the program exists, it was built with a
// window system, and there is a display for it to use. The last is asked
// of the environment rather than of the program, because starting a
// program to find out costs a fork.
fun available() {
  const path = executable();
  if (path == nil) { return false; }
  if (platform() != "darwin" and env("DISPLAY") == nil and
      env("WAYLAND_DISPLAY") == nil) {
    return false;
  }
  const result = run([path, "--version"]);
  if (result["code"] != 0) { return false; }
  return !result["out"].contains("(none)");
}

// A colour as the protocol writes it: 0xRRGGBB as a decimal number, or
// -1 for the window's own.
fun packed(value) {
  const n = value.packed();
  if (n == nil) { return "-1"; }
  return str(n);
}

class Window < Backend {
  init(options = nil) {
    super.init("native");
    if (options == nil) { options = {}; }
    this.title = options.get("title", "andy");
    this.want_cols = options.get("cols", 100);
    this.want_rows = options.get("rows", 30);
    this.path = options.get("path", nil);
    this.listener = nil;
    this.link = nil;
    this.buffer = "";
    this.cached = geom.Size(this.want_cols, this.want_rows);
    this.previous = nil;
    this.closed = false;
    // How long to wait for the window to appear before giving up. A
    // window system that is busy can take a moment; one that is not
    // there at all never answers.
    this.patience = options.get("patience", 10);
    // How long a frame may take to go out. Generous: the other end is
    // on the same machine and reading as fast as it can, so reaching
    // this at all means the window has stopped answering.
    this.write_patience = options.get("write_patience", 2);
    this.trace = env("ANDY_TRACE");
  }

  // The window's own colours, which are true colour and always have
  // been: there is no palette here to reduce anything to.
  depth() { return color.Depth.TrueColor; }

  has_mouse() { return true; }

  start() {
    let program = this.path;
    if (program == nil) { program = executable(); }
    if (program == nil) {
      throw error("andy-gui was not found: build it, or set $ANDY_GUI",
                  nil, "io");
    }

    // We listen and it connects, rather than the other way round, so
    // that the port is known before the program starts and there is
    // nothing to guess or retry.
    this.listener = tcp_listen(0, 1, "127.0.0.1");
    const port = this.listener.port();

    // Started in the background, with its output sent nowhere.
    //
    // Both halves of that matter. run() would wait for the program to
    // finish, and it is meant to outlive the call by the length of the
    // program; and a child that inherited the pipe shell() reads would
    // hold that pipe open for as long as the window was up, so shell()
    // would wait for it anyway. Detached and redirected, the shell has
    // nothing left to wait for and returns at once.
    const command = quoted(program) + " --port " + str(port) +
                    " --title " + quoted(this.title) +
                    " --cols " + str(this.want_cols) +
                    " --rows " + str(this.want_rows) +
                    " >/dev/null 2>&1 &";
    shell(command);

    this.listener.set_timeout(this.patience);
    try {
      this.link = this.listener.accept();
    } catch (e: "timeout") {
      this.link = nil;
    }
    if (this.link == nil) {
      this.listener.close();
      this.listener = nil;
      throw error("andy-gui did not open a window", nil, "io");
    }
    // From here on, a read that would wait gives up at once: the event
    // loop asks what has arrived and sleeps between asks, so no worker
    // thread is ever held inside a socket call on the window's behalf.
    this.link.set_timeout(0.001);

    // It says how big the window turned out to be before anything else.
    const greeting = this.wait_for("ready");
    if (greeting == nil) {
      this.stop();
      throw error("andy-gui did not report a window", nil, "io");
    }
    this.cached = geom.Size(floor(num(greeting[1])), floor(num(greeting[2])));
    this.running = true;
    this.previous = nil;
    return this;
  }

  // Reads lines until one starts with `word`, and gives back its fields.
  // Anything else that arrives first is kept for poll() to find.
  wait_for(word) {
    const deadline = time() + this.patience;
    const keep = [];
    while (time() < deadline) {
      const line = this.read_line();
      if (line == nil) {
        sleep(0.01);
        continue;
      }
      const fields = line.split(" ");
      if (fields[0] == word) {
        for (let held in keep) { this.buffer = held + "\n" + this.buffer; }
        return fields;
      }
      keep.push(line);
    }
    return nil;
  }

  read_line() {
    const cut = this.buffer.find("\n");
    if (cut >= 0) {
      const line = this.buffer.sub(0, cut);
      this.buffer = this.buffer.sub(cut + 1);
      return line;
    }
    if (this.link == nil) { return nil; }
    let arrived = nil;
    try {
      arrived = this.link.read(8192);
    } catch (e: "timeout") {
      return nil;
    } catch (e: "net") {
      this.closed = true;
      return nil;
    }
    if (arrived == nil) {
      // The window closed its side. Say so once; poll() turns it into a
      // Close event and the application decides what that means.
      this.closed = true;
      return nil;
    }
    if (arrived == "") { return nil; }
    this.buffer += arrived;
    return this.read_line();
  }

  send(line) {
    // Nothing is written to a window that has already gone. The socket
    // error would be caught below, but the signal that comes with it
    // would not be, and a program should not die of a window closing.
    if (this.link == nil or this.closed) { return this; }
    try {
      // A socket has one timeout for both directions, and the two want
      // very different things. A read must not wait at all, because the
      // event loop asks what has arrived and sleeps between asks; a
      // write must wait, because a whole frame is tens of kilobytes and
      // a write that gives up halfway delivers half a frame. So the
      // timeout is widened for the write and narrowed again after it.
      this.link.set_timeout(this.write_patience);
      this.link.write(line + "\n");
    } catch (e: "net") {
      // The window has gone. The next poll reports it; there is nothing
      // useful to do about a frame that could not be delivered.
      this.closed = true;
    } catch (e: "timeout") {
      this.closed = true;
    } finally {
      if (this.link != nil) { this.link.set_timeout(0.001); }
    }
    return this;
  }

  stop() {
    if (!this.running and this.link == nil) { return this; }
    this.running = false;
    if (this.link != nil) {
      this.send("bye");
      this.link.close();
      this.link = nil;
    }
    if (this.listener != nil) {
      this.listener.close();
      this.listener = nil;
    }
    return this;
  }

  size() { return this.cached; }

  // Sends the runs that differ from the last frame, the same comparison
  // the terminal backend makes and for the same reason: a frame is a few
  // hundred bytes when a caret moved and a few thousand when the whole
  // window changed.
  present(surface) {
    if (!this.running or this.link == nil) { return this; }
    if (surface.width != this.cached.width or
        surface.height != this.cached.height) {
      this.previous = nil;
    }
    const runs = surface.diff(this.previous);
    if (runs.len() == 0 and this.previous != nil) { return this; }

    const parts = ["frame"];
    for (let [x, y, run_style, cells] in runs) {
      let fg = run_style.fg;
      let bg = run_style.bg;
      parts.push("run " + str(x) + " " + str(y) + " " + packed(fg) + " " +
                 packed(bg) + " " + str(run_style.attrs) + " " +
                 cells.join(""));
    }
    if (surface.cursor == nil) {
      parts.push("cursor off");
    } else {
      parts.push("cursor " + str(surface.cursor.x) + " " +
                 str(surface.cursor.y));
    }
    parts.push("end");
    const payload = parts.join("\n");
    // $ANDY_TRACE names a file to append every frame to, which is how a
    // disagreement between the two halves is settled: what andy sent is
    // then a text file next to what the window drew.
    if (this.trace != nil) { append_file(this.trace, payload + "\n"); }
    this.send(payload);
    this.previous = surface.snapshot();
    return this;
  }

  poll() {
    const events = [];
    if (!this.running) { return events; }
    if (this.closed) {
      this.closed = false;
      events.push(event.Close());
      return events;
    }
    for (;;) {
      const line = this.read_line();
      if (line == nil) { break; }
      const one = this.decode(line);
      if (one != nil) { events.push(one); }
    }
    return events;
  }

  decode(line) {
    const fields = line.split(" ");
    switch (fields[0]) {
      case "size": {
        this.cached = geom.Size(floor(number_at(fields, 1, 80)),
                                floor(number_at(fields, 2, 24)));
        this.previous = nil;
        return event.Resize(this.cached.width, this.cached.height);
      }
      case "key": {
        const name = fields[1];
        const ctrl = number_at(fields, 2, 0) != 0;
        const alt = number_at(fields, 3, 0) != 0;
        const shift = number_at(fields, 4, 0) != 0;
        // What the key typed is the rest of the line, because it may
        // contain a space and may be several bytes of UTF-8.
        let typed = "";
        if (fields.len() > 5) { typed = fields.slice(5).join(" "); }
        return event.Key(name, typed, ctrl, alt, shift);
      }
      case "mouse": {
        return event.Mouse(fields[1], floor(number_at(fields, 2, 0)),
                           floor(number_at(fields, 3, 0)),
                           floor(number_at(fields, 4, 0)),
                           floor(number_at(fields, 5, 0)),
                           number_at(fields, 6, 0) != 0,
                           number_at(fields, 7, 0) != 0,
                           number_at(fields, 8, 0) != 0);
      }
      case "focus": return event.Focus(number_at(fields, 1, 0) != 0);
      case "close": return event.Close();
    }
    return nil;
  }

  set_clipboard(value) {
    // One line, so a newline in the text would end it early. Text with
    // one in it is sent with the newlines turned into spaces rather than
    // not sent at all, which is the lesser of the two disappointments.
    this.send("clip " + value.replace("\n", " "));
    return true;
  }

  set_title(value) {
    this.title = value;
    this.send("title " + value);
    return this;
  }

  bell() {
    this.send("bell");
    return this;
  }
}

fun number_at(fields, index, fallback) {
  if (index >= fields.len()) { return fallback; }
  const value = num(fields[index]);
  if (value == nil) { return fallback; }
  return value;
}

// A word the shell will pass through as one argument. The title comes
// from the program and the path from the environment, so neither is
// beyond suspicion.
fun quoted(value) {
  return "'" + value.replace("'", "'\\''") + "'";
}
