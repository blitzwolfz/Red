// The loop: events in, a frame out.
//
//   import "andy/app" as app;
//
//   const window = app.App(root);
//   window.bind("ctrl+q", fun () { window.quit(); });
//   window.run();
//
// An application owns a backend, a widget tree, the focus, and a stack
// of things floating above the tree — dialogs, menus, dropdowns and
// toasts. Its job is to turn a stream of events into calls on widgets,
// and a widget tree into a frame.
//
// The loop only draws when something changed. A form sitting on the
// screen with nobody touching it costs one poll and a sleep per pass,
// which is a few microseconds; a widget that wants to animate says so by
// marking itself dirty, and one that wants the clock asks for a timer.
// Nothing spins.

import "andy/geom" as geom;
import "andy/event" as event;
import "andy/color" as color;
import "andy/style" as style;
import "andy/text" as text;
import "andy/canvas" as canvas;
import "andy/theme" as theme;
import "andy/widget" as widget;
import "andy/widgets" as widgets;
import "andy/views" as views;
import "andy/backend" as backend;
import "andy/term" as term;

const Widget = widget.Widget;
const Rect = geom.Rect;
const Size = geom.Size;

// Something floating above the tree: a dialog, a menu, a toast.
//
// `modal` decides what happens to everything underneath. A modal layer
// takes every key and every click, and what is behind it is dimmed; a
// layer that is not modal is drawn on top and otherwise ignored, which
// is what a toast is.
class Layer {
  init(root, modal = true, placement = nil) {
    this.root = root;
    this.modal = modal;
    // A rectangle in screen coordinates, or nil to centre it.
    this.placement = placement;
    this.on_close = nil;
    this.dim = modal;
    this.focus = nil;
    this.shadow = modal;
    // Set when the layer should go away at the end of this pass. A
    // layer cannot remove itself while its own handler is running,
    // because the loop is holding it.
    this.closing = false;
    this.expires = 0;
    // What escape, or a click outside, should report. A dialog sets it
    // to the same thing its cancel button does, so that the three ways
    // of saying no all say the same thing.
    this.escape = nil;
  }

  close() {
    this.closing = true;
    return this;
  }

  str() { return "<layer>"; }
}

class App {
  init(root = nil, options = nil) {
    if (options == nil) { options = {}; }
    this.root = root;
    if (this.root == nil) { this.root = Widget(); }
    this.screen = nil;
    this.theme = options.get("theme", nil);
    this.title = options.get("title", nil);
    this.running = false;
    this.exit_code = 0;

    this.surface = nil;
    this.focus = nil;
    this.layers = [];
    // Key bindings that apply wherever the focus is, as name to handler.
    this.bindings = {};
    this.timers = [];
    this.next_timer = 1;
    this.pointer = nil;
    this.hovered = nil;

    // How long a pass of the loop waits when nothing is happening.
    // Short enough that a keystroke feels immediate, long enough that an
    // idle program does not warm the room.
    this.idle = options.get("idle", 0.008);
    this.needs_draw = true;
    this.needs_layout = true;
    this.frames = 0;
    this.started_at = 0;
    // Called once a frame, before drawing, with the application. Where
    // a program puts anything it wants to do on every pass.
    this.on_frame = nil;
    // Called when the window is asked to close. Return false to refuse,
    // which is how "you have unsaved changes" is written.
    this.on_close = nil;

    this.bind("tab", fun () { this.focus_next(); });
    this.bind("shift+tab", fun () { this.focus_previous(); });
  }

  // ---- setting it up ----

  set_root(value) {
    this.root = value;
    this.focus = nil;
    this.needs_layout = true;
    this.needs_draw = true;
    return this;
  }

  // A key that works wherever the focus is. The description is the one
  // event.Key.matches understands: "ctrl+q", "alt+enter", "f5".
  //
  // Bindings are tried after the focused widget and its parents, so a
  // text field still gets its own ctrl+a and a binding is a fallback
  // rather than an ambush.
  bind(description, handler) {
    this.bindings[description.lower()] = handler;
    return this;
  }

  unbind(description) {
    this.bindings.remove(description.lower());
    return this;
  }

  // ---- the frame's context ----

  context() {
    const ui = widget.Context(this, this.current_theme(),
                              this.screen.has_mouse(), this.screen.depth(),
                              time());
    ui.focus = this.focus;
    ui.pointer = this.pointer;
    return ui;
  }

  current_theme() {
    if (this.theme != nil) { return this.theme; }
    return theme.for_depth(this.screen.depth());
  }

  set_theme(value) {
    this.theme = value;
    this.needs_draw = true;
    this.invalidate_all();
    return this;
  }

  invalidate_all() {
    this.root.walk(fun (w) { w.dirty = true; });
    for (let layer in this.layers) {
      layer.root.walk(fun (w) { w.dirty = true; });
    }
    this.needs_draw = true;
    return this;
  }

  // ---- focus ----

  // The tree the keyboard is currently in: the topmost modal layer, or
  // the window itself when there is none.
  active_root() {
    for (let i in range(0, this.layers.len())) {
      const layer = this.layers[this.layers.len() - 1 - i];
      if (layer.modal) { return layer.root; }
    }
    return this.root;
  }

  focus_ring() { return this.active_root().focus_ring(); }

  focus_on(w) {
    if (this.focus == w) { return this; }
    const previous = this.focus;
    this.focus = w;
    const ui = this.context();
    if (previous != nil) { previous.on_blur(ui); }
    if (w != nil) { w.on_focus(ui); }
    this.needs_draw = true;
    return this;
  }

  // Moves the focus along the ring, wrapping round. The ring is
  // recomputed each time rather than cached, because a widget that was
  // disabled or hidden since the last move should not be visited and a
  // cache would not know.
  move_focus(by) {
    const ring = this.focus_ring();
    if (ring.len() == 0) {
      this.focus_on(nil);
      return this;
    }
    let at = ring.index_of(this.focus);
    if (at < 0) {
      if (by < 0) { at = ring.len(); } else { at = -1; }
    }
    const next = ((at + by) % ring.len() + ring.len()) % ring.len();
    this.focus_on(ring[next]);
    return this;
  }

  focus_next() { return this.move_focus(1); }
  focus_previous() { return this.move_focus(-1); }

  // Puts the focus on the first thing that will take it, which is what
  // happens when a window or a dialog opens.
  focus_first() {
    const ring = this.focus_ring();
    if (ring.len() == 0) { return this.focus_on(nil); }
    return this.focus_on(ring[0]);
  }

  // The widget with this id, in the window or in any layer.
  find(id) {
    const found = this.root.find(id);
    if (found != nil) { return found; }
    for (let layer in this.layers) {
      const inside = layer.root.find(id);
      if (inside != nil) { return inside; }
    }
    return nil;
  }

  // ---- layers ----

  // Puts a widget above the window. Returns the layer, which is what
  // closes it again.
  show(root, modal = true, placement = nil) {
    const layer = Layer(root, modal, placement);
    if (modal) { layer.focus = this.focus; }
    this.layers.push(layer);
    this.needs_layout = true;
    this.needs_draw = true;
    if (modal) {
      this.focus = nil;
      this.focus_first();
    }
    return layer;
  }

  // Closes the topmost layer, or a particular one.
  dismiss(layer = nil) {
    if (this.layers.len() == 0) { return this; }
    let target = layer;
    if (target == nil) { target = this.layers[this.layers.len() - 1]; }
    const at = this.layers.index_of(target);
    if (at < 0) { return this; }
    this.layers.remove(at);
    if (target.on_close != nil) { target.on_close(target); }
    // Give the keyboard back to whatever had it before the layer opened,
    // if that widget is still there and still willing.
    if (target.modal) {
      this.focus = nil;
      if (target.focus != nil and target.focus.accepts_focus()) {
        this.focus_on(target.focus);
      } else {
        this.focus_first();
      }
    }
    this.needs_layout = true;
    this.needs_draw = true;
    return this;
  }

  // Opens one of a menu bar's menus, under its title.
  open_menu(bar, index) {
    if (index < 0 or index >= bar.menus.len()) { return this; }
    bar.open_at = index;
    bar.invalidate();
    const where = bar.absolute_frame();
    const places = bar.positions();
    const menu = views.Menu(bar.menus[index]);
    const size = menu.measure(this.screen.size());
    const x = geom.clamp(where.x + places[index][0], 0,
                         max(0, this.screen.size().width - size.width));
    const layer = this.show(menu, true,
                            Rect(x, where.bottom(), size.width, size.height));
    layer.dim = false;
    menu.on_choose = fun (item, m) { this.dismiss(layer); };
    layer.on_close = fun (l) {
      bar.open_at = -1;
      bar.invalidate();
    };
    return this;
  }

  // A menu at a point, for a right click.
  open_context_menu(items, x, y) {
    const menu = views.Menu(items);
    const size = menu.measure(this.screen.size());
    const screen = this.screen.size();
    const at = Rect(geom.clamp(x, 0, max(0, screen.width - size.width)),
                    geom.clamp(y, 0, max(0, screen.height - size.height)),
                    size.width, size.height);
    const layer = this.show(menu, true, at);
    layer.dim = false;
    menu.on_choose = fun (item, m) { this.dismiss(layer); };
    return layer;
  }

  // A message that appears at the bottom for a few seconds.
  //
  //   window.toast("Saved");
  //   window.toast("Could not write the file", "error");
  toast(message, kind = "info", seconds = 3) {
    const note = views.Toast(message, kind, seconds);
    const size = note.measure(this.screen.size());
    const screen = this.screen.size();
    const at = Rect(max(0, screen.width - size.width - 2),
                    max(0, screen.height - size.height - 2),
                    min(size.width, screen.width),
                    min(size.height, screen.height));
    const layer = this.show(note, false, at);
    layer.shadow = true;
    layer.expires = time() + seconds;
    return layer;
  }

  // ---- dialogs ----

  // A message with one button.
  message(title, body, on_close = nil) {
    const label = widgets.Label(body).wrapping();
    const ok = widgets.Button("OK").as_primary();
    const panel = widgets.Panel(title,
        widgets.Column(label.growing(1),
                       widgets.Row(widgets.Spacer(), ok, widgets.Spacer()))
            .spaced(1).padded(geom.uniform(1)));
    panel.fill = "dialog";
    const layer = this.show(dialog_sized(panel, this.screen.size(), body));
    ok.on_press = fun (b) {
      this.dismiss(layer);
      if (on_close != nil) { on_close(); }
    };
    this.bind_escape(layer, on_close);
    return layer;
  }

  // A question with two buttons. `answer` is called with true or false.
  confirm(title, body, answer) {
    const label = widgets.Label(body).wrapping();
    const yes = widgets.Button("Yes").as_primary();
    const no = widgets.Button("No");
    const panel = widgets.Panel(title,
        widgets.Column(label.growing(1),
                       widgets.Row(widgets.Spacer(), yes, widgets.Gap(2), no,
                                   widgets.Spacer()))
            .spaced(1).padded(geom.uniform(1)));
    panel.fill = "dialog";
    const layer = this.show(dialog_sized(panel, this.screen.size(), body));
    yes.on_press = fun (b) {
      this.dismiss(layer);
      answer(true);
    };
    no.on_press = fun (b) {
      this.dismiss(layer);
      answer(false);
    };
    this.bind_escape(layer, fun () { answer(false); });
    return layer;
  }

  // A question with a text field. `answer` is called with the text, or
  // with nil when it was cancelled.
  prompt(title, body, answer, initial = "") {
    const label = widgets.Label(body).wrapping();
    const field = widgets.Input(initial);
    const ok = widgets.Button("OK").as_primary();
    const cancel = widgets.Button("Cancel");
    const panel = widgets.Panel(title,
        widgets.Column(label, field,
                       widgets.Row(widgets.Spacer(), ok, widgets.Gap(2),
                                   cancel))
            .spaced(1).padded(geom.uniform(1)));
    panel.fill = "dialog";
    const layer = this.show(dialog_sized(panel, this.screen.size(), body));
    const accept = fun (b) {
      const value = field.get_value();
      this.dismiss(layer);
      answer(value);
    };
    ok.on_press = accept;
    field.on_submit = accept;
    cancel.on_press = fun (b) {
      this.dismiss(layer);
      answer(nil);
    };
    this.bind_escape(layer, fun () { answer(nil); });
    this.focus_on(field);
    return layer;
  }

  // A list to pick from, which is what most "choose one of these"
  // questions want rather than a dialog with twelve buttons.
  choose(title, options, answer, labelled = nil) {
    const list = views.List(options);
    if (labelled != nil) { list.labelled(labelled); }
    const panel = widgets.Panel(title, list.growing(1).padded(geom.uniform(1)));
    panel.fill = "dialog";
    const screen = this.screen.size();
    const height = min(max(6, options.len() + 4), max(6, screen.height - 4));
    let width = 24;
    for (let option in options) {
      let caption = str(option);
      if (labelled != nil) { caption = str(labelled(option)); }
      width = max(width, text.width(caption) + 8);
    }
    const layer = this.show(panel, true,
        Rect(0, 0, min(width, screen.width), height));
    layer.placement = screen_center(screen, min(width, screen.width), height);
    list.on_activate = fun (view, index) {
      this.dismiss(layer);
      answer(options[index]);
    };
    this.bind_escape(layer, fun () { answer(nil); });
    this.focus_on(list);
    return layer;
  }

  // Escape closes a dialog and reports the same thing its cancel button
  // would. Written once here so that every dialog agrees.
  bind_escape(layer, on_cancel) {
    layer.escape = on_cancel;
    return layer;
  }

  // ---- events ----

  // Sends one event through the application, exactly as the loop would.
  // A test drives an interface with this and never starts a loop at all.
  dispatch(one) {
    const ui = this.context();
    switch (one.kind) {
      case event.Kind.Key: return this.dispatch_key(one, ui);
      case event.Kind.Mouse: return this.dispatch_mouse(one, ui);
      case event.Kind.Resize: {
        this.needs_layout = true;
        this.needs_draw = true;
        this.invalidate_all();
        return true;
      }
      case event.Kind.Paste: return this.dispatch_paste(one, ui);
      case event.Kind.Close: {
        this.request_close();
        return true;
      }
      case event.Kind.Focus: {
        this.needs_draw = true;
        return true;
      }
    }
    return false;
  }

  dispatch_key(key, ui) {
    // The focused widget first, then everything containing it. A widget
    // that does not know what a key means passes it outwards, which is
    // how a dialog handles escape without its buttons knowing about it.
    let at = this.focus;
    while (at != nil) {
      if (at.enabled and at.on_key_event(key, ui)) {
        this.needs_draw = true;
        return true;
      }
      at = at.parent;
    }

    // A modal layer's own escape, before the global bindings, so that a
    // dialog closes rather than the program quitting.
    const top = this.top_modal();
    if (top != nil and key.matches("escape")) {
      const cancel = top.escape;
      this.dismiss(top);
      if (cancel != nil) { cancel(); }
      return true;
    }

    for (let [description, handler] in this.bindings.entries()) {
      if (key.matches(description)) {
        handler();
        this.needs_draw = true;
        return true;
      }
    }
    return false;
  }

  dispatch_paste(paste, ui) {
    let at = this.focus;
    while (at != nil) {
      // A paste is offered as a paste to anything that understands one,
      // and as text to a field that does not, which covers every widget
      // that can hold text without each one having to say so.
      if (at.enabled and at.accepts_text()) {
        at.insert(paste.text);
        this.needs_draw = true;
        return true;
      }
      at = at.parent;
    }
    return false;
  }

  top_modal() {
    for (let i in range(0, this.layers.len())) {
      const layer = this.layers[this.layers.len() - 1 - i];
      if (layer.modal) { return layer; }
    }
    return nil;
  }

  // The overlays open in the tree the keyboard is in, as
  // [widget, rectangle] in screen coordinates.
  overlays() {
    const found = [];
    const screen = this.screen.size();
    const collect = fun (w) {
      if (!w.has_overlay()) { return; }
      const where = w.absolute_frame();
      const wanted = w.overlay_size();
      const width = min(wanted.width, screen.width);
      // Below if it fits, above if it does not, and pinned to the top
      // when it fits in neither, which is the best that can be done on a
      // screen shorter than the list.
      let y = where.bottom();
      if (y + wanted.height > screen.height) {
        const above = where.y - wanted.height;
        if (above >= 0) { y = above; }
        else { y = max(0, screen.height - wanted.height); }
      }
      const x = geom.clamp(where.x, 0, max(0, screen.width - width));
      found.push([w, Rect(x, y, width, min(wanted.height, screen.height))]);
    };
    this.active_root().walk(collect);
    return found;
  }

  dispatch_mouse(mouse, ui) {
    this.pointer = geom.Point(mouse.x, mouse.y);

    // An open dropdown gets the mouse before anything else, because it
    // is drawn over everything else.
    for (let [w, rect] in this.overlays()) {
      if (rect.contains(mouse.x, mouse.y)) {
        if (w.on_overlay_mouse(mouse, ui, rect)) {
          this.needs_draw = true;
          return true;
        }
        return true;
      }
      // A click outside an open dropdown closes it rather than reaching
      // what is underneath, which is what every dropdown does.
      if (mouse.is_press()) {
        w.open = false;
        w.invalidate();
        this.needs_draw = true;
        return true;
      }
    }

    // Layers from the top down. A modal layer swallows anything outside
    // itself; a click outside a non modal one falls through to whatever
    // is under it.
    for (let i in range(0, this.layers.len())) {
      const layer = this.layers[this.layers.len() - 1 - i];
      const where = this.layer_frame(layer);
      if (where.contains(mouse.x, mouse.y)) {
        const inside = mouse.relative_to(where);
        if (this.send_mouse(layer.root, inside, ui)) { return true; }
        return layer.modal;
      }
      if (layer.modal) {
        // Clicking outside a menu closes it, which is what a menu is
        // for; clicking outside a dialog does nothing, because a dialog
        // is a question that has to be answered.
        if (mouse.is_press() and !layer.dim) {
          const cancel = layer.escape;
          this.dismiss(layer);
          if (cancel != nil) { cancel(); }
        }
        return true;
      }
    }

    return this.send_mouse(this.root, mouse, ui);
  }

  // Finds what is under the pointer and offers it the event, then its
  // parents, the same way a key travels.
  send_mouse(root, mouse, ui) {
    const found = root.hit(mouse.x - root.frame.x, mouse.y - root.frame.y);
    if (found == nil) {
      this.set_hovered(nil);
      return false;
    }
    const [target, x, y] = found;
    if (mouse.action == "move") { this.set_hovered(target); }

    let at = target;
    let local = event.Mouse(mouse.action, x, y, mouse.button, mouse.wheel,
                            mouse.ctrl, mouse.alt, mouse.shift);
    while (at != nil) {
      if (at.enabled and at.on_mouse_event(local, ui)) {
        this.needs_draw = true;
        return true;
      }
      local = event.Mouse(local.action, local.x + at.frame.x,
                          local.y + at.frame.y, local.button, local.wheel,
                          local.ctrl, local.alt, local.shift);
      at = at.parent;
    }
    return false;
  }

  set_hovered(w) {
    if (this.hovered == w) { return this; }
    if (this.hovered != nil) {
      this.hovered.hovered = false;
      this.hovered.invalidate();
    }
    this.hovered = w;
    if (w != nil) {
      w.hovered = true;
      w.invalidate();
    }
    this.needs_draw = true;
    return this;
  }

  // ---- timers ----

  // Calls `body` after `seconds`. Returns a handle for cancel().
  after(seconds, body) {
    const id = this.next_timer;
    this.next_timer += 1;
    this.timers.push({"id": id, "due": time() + seconds, "every": nil,
                      "body": body});
    return id;
  }

  // Calls `body` every `seconds`, starting one interval from now.
  every(seconds, body) {
    const id = this.next_timer;
    this.next_timer += 1;
    this.timers.push({"id": id, "due": time() + seconds, "every": seconds,
                      "body": body});
    return id;
  }

  cancel(id) {
    for (let i in range(0, this.timers.len())) {
      if (this.timers[i]["id"] == id) {
        this.timers.remove(i);
        return true;
      }
    }
    return false;
  }

  run_timers() {
    if (this.timers.len() == 0) { return this; }
    const now = time();
    const due = [];
    for (let timer in this.timers) {
      if (timer["due"] <= now) { due.push(timer); }
    }
    for (let timer in due) {
      if (timer["every"] == nil) {
        this.cancel(timer["id"]);
      } else {
        timer["due"] = now + timer["every"];
      }
      timer["body"]();
    }
    return this;
  }

  // ---- layout and drawing ----

  layer_frame(layer) {
    const screen = this.screen.size();
    if (layer.placement != nil) { return layer.placement; }
    const wanted = layer.root.measure(screen);
    return screen_center(screen, min(wanted.width, screen.width),
                         min(wanted.height, screen.height));
  }

  layout() {
    const screen = this.screen.size();
    if (this.surface == nil or this.surface.width != screen.width or
        this.surface.height != screen.height) {
      this.surface = canvas.Canvas(screen.width, screen.height,
                                   this.current_theme().get("background"));
    }
    this.root.arrange(Rect(0, 0, screen.width, screen.height));
    for (let layer in this.layers) {
      layer.root.arrange(this.layer_frame(layer));
    }
    this.needs_layout = false;
    return this;
  }

  draw() {
    const ui = this.context();
    this.surface.base = this.current_theme().get("background");
    this.surface.reset();
    this.root.draw(this.surface, ui);

    // Dropdowns before the layers, so that a dropdown inside a dialog is
    // still under the next dialog.
    this.draw_overlays(ui);

    const screen = this.screen.size();
    for (let layer in this.layers) {
      if (layer.dim) {
        // Everything behind a modal layer is faded, which says "this is
        // the only thing you can touch" without a word.
        const background = this.current_theme().background();
        this.surface.map_style(Rect(0, 0, screen.width, screen.height),
            fun (existing) {
              return style.Style(existing.fg.mix(background, 0.55),
                                 existing.bg.mix(background, 0.35),
                                 existing.attrs & ~style.BOLD);
            });
      }
      const where = layer.root.frame;
      if (layer.shadow) { this.surface.shadow(where); }
      this.surface.frame(where, fun () { layer.root.draw(this.surface, ui); });
    }
    if (this.layers.len() > 0) { this.draw_overlays(ui); }
    this.needs_draw = false;
    this.frames += 1;
    return this;
  }

  draw_overlays(ui) {
    for (let [w, rect] in this.overlays()) {
      this.surface.shadow(rect);
      w.draw_overlay(this.surface, ui, rect);
    }
    return this;
  }

  // ---- the loop ----

  // Asks to stop. The close handler may refuse, which is how a program
  // with unsaved work argues about it.
  request_close() {
    if (this.on_close != nil and this.on_close(this) == false) {
      return this;
    }
    return this.quit();
  }

  quit(code = 0) {
    this.exit_code = code;
    this.running = false;
    return this;
  }

  // Runs one pass: events, timers, expiry, layout, draw. Returns whether
  // the application is still running.
  //
  // Separate from run() so that a test can step an interface one pass at
  // a time, and so that a program with a loop of its own can drive andy
  // from inside it.
  step() {
    for (let one in this.screen.poll()) { this.dispatch(one); }
    this.run_timers();
    this.expire_layers();
    if (this.on_frame != nil) { this.on_frame(this); }

    if (this.root.dirty) { this.needs_layout = true; }
    for (let layer in this.layers) {
      if (layer.root.dirty) { this.needs_layout = true; }
    }
    if (this.needs_layout) {
      this.layout();
      this.needs_draw = true;
    }
    if (this.needs_draw) {
      this.draw();
      this.screen.present(this.surface);
    }
    return this.running;
  }

  expire_layers() {
    if (this.layers.len() == 0) { return this; }
    const now = time();
    const going = [];
    for (let layer in this.layers) {
      if (layer.closing) { going.push(layer); }
      else if (layer.expires > 0 and layer.expires <= now) { going.push(layer); }
    }
    for (let layer in going) { this.dismiss(layer); }
    return this;
  }

  // Takes the screen, runs until something asks to stop, and gives the
  // screen back however it ends.
  //
  // `screen` is a backend; with none, one is chosen: the terminal when
  // there is one, and an error when there is not, because a program that
  // silently drew an interface into a pipe would be worse than one that
  // said it could not.
  run(screen = nil) {
    if (screen == nil) { screen = choose_backend(this.title); }
    this.screen = screen;
    this.screen.start();
    this.running = true;
    this.started_at = time();
    this.needs_layout = true;
    this.needs_draw = true;
    try {
      this.layout();
      if (this.focus == nil) { this.focus_first(); }
      while (this.running) {
        this.step();
        // Nothing to do: wait a little rather than asking again at once.
        // This is the whole of andy's idle cost.
        if (!this.needs_draw and !this.needs_layout) { sleep(this.idle); }
      }
    } finally {
      this.screen.stop();
    }
    return this.exit_code;
  }

  // Copies text, if the screen can.
  copy(value) { return this.screen.set_clipboard(value); }

  bell() {
    this.screen.bell();
    return this;
  }

  str() { return "<app>"; }
}

// A rectangle of this size in the middle of the screen, a little above
// the true centre because a dialog centred exactly looks low.
fun screen_center(screen, width, height) {
  const y = max(0, floor((screen.height - height) * 2 / 5));
  return Rect(max(0, floor((screen.width - width) / 2)), y, width, height);
}

// A dialog wide enough for its message and no wider than the screen.
// Measured rather than guessed, because a message with a long word in it
// wraps differently from one without.
fun dialog_sized(panel, screen, body) {
  const width = min(max(32, text.width(body) + 8),
                    max(20, screen.width - 8));
  panel.min_width = width;
  return panel;
}

// The backend to use when the program did not say.
//
// A terminal when there is one. Otherwise there is nothing to draw on,
// and saying so is better than drawing escape sequences into a log file
// for somebody to find later.
fun choose_backend(title = nil) {
  const wanted = env("ANDY_BACKEND", "auto").lower();
  if (wanted == "headless") { return backend.Headless(); }
  if (wanted == "terminal" or wanted == "auto") {
    if (term.available()) { return term.Terminal({"title": title}); }
    if (wanted == "terminal") {
      throw error("no terminal to draw on", nil, "io");
    }
  }
  throw error("andy has no screen to draw on: not a terminal, and " +
              "$ANDY_BACKEND names none",
              nil, "io");
}
