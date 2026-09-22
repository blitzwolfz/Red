// What every widget is.
//
//   import "andy/widget" as widget;
//
//   class Blinker < widget.Widget {
//     measure(available) { return geom.Size(3, 1); }
//     draw(surface, ui) { surface.text(0, 0, "...", ui.style("text")); }
//   }
//
// A widget is an object with a place in a tree. It is measured, then
// arranged, then drawn, and it is offered events. Those four things are
// the whole contract, and a widget that overrides none of them is a
// blank rectangle that does nothing, which is a reasonable thing to
// start from.
//
// The tree is retained: a widget is built once and kept, and changing
// what it shows means changing a field on it. That is the shape that
// suits a program with state — a form, an editor, a dashboard — because
// the state lives where it is used rather than being threaded through a
// render function on every frame.
//
// Coordinates. A widget is arranged into a rectangle in its parent's
// coordinates, which it keeps as `frame`, and is drawn in its own, where
// its top left corner is 0, 0. The canvas does the translating.

import "andy/geom" as geom;
import "andy/event" as event;
import "andy/style" as style;
import "andy/theme" as theme;
import "andy/canvas" as canvas;

const Rect = geom.Rect;
const Size = geom.Size;

// What a widget is given when it draws or handles an event: the theme,
// what the screen can do, and a way back to the application.
//
// One of these is made per frame and passed down the tree, so a widget
// never reaches for a global and a test can drive a widget with a
// context it made itself.
class Context {
  init(app = nil, ui_theme = nil, mouse = true, depth = 0, now = 0) {
    this.app = app;
    this.theme = ui_theme;
    if (this.theme == nil) { this.theme = theme.default_theme(); }
    this.mouse = mouse;
    this.depth = depth;
    this.now = now;
    // The widget the keyboard is talking to, so that a container can
    // draw itself differently when the focus is somewhere inside it.
    this.focus = nil;
    // Where the pointer is, or nil. Widgets use it for hover states,
    // and nothing draws a hover state when the backend has no mouse.
    this.pointer = nil;
  }

  // A style by name, from the theme.
  style(name) { return this.theme.get(name); }

  // The border set the theme draws frames with.
  border() { return this.theme.border_set; }

  // Is `w` the widget the keyboard is talking to?
  is_focused(w) { return this.focus == w; }

  // Is the focus inside `w`, at any depth? A frame uses this to light up
  // when anything in it is focused, which is how a user finds the
  // keyboard on a screen with four panes.
  holds_focus(w) {
    let at = this.focus;
    while (at != nil) {
      if (at == w) { return true; }
      at = at.parent;
    }
    return false;
  }

  str() { return "<context ${this.theme.name}>"; }
}

class Widget {
  init() {
    this.parent = nil;
    this.children = [];
    // Where the parent put us, in the parent's coordinates. Set by
    // arrange() and read by hit testing and by anything that wants to
    // know how much room it ended up with.
    this.frame = Rect(0, 0, 0, 0);

    this.visible = true;
    this.enabled = true;
    // Can the keyboard reach this? A label cannot, a button can. Set to
    // false on a button to make it unreachable as well as unclickable.
    this.focusable = false;
    // Share of leftover room along the parent's axis. Zero means "what
    // I asked for and no more".
    this.flex = 0;
    this.min_width = 0;
    this.min_height = 0;
    this.max_width = nil;
    this.max_height = nil;
    this.padding = geom.NONE;
    // A name, for finding a widget again in a tree somebody else built.
    this.id = nil;
    // A style name applied under whatever the widget draws, for a
    // background that differs from the window's.
    this.fill = nil;
    // Set by the widget when what it shows has changed and the screen
    // should be redrawn. The application collects these; a program that
    // only redraws on demand is a program that does not burn a core
    // showing a static form.
    this.dirty = true;
    // Set while the pointer is over it, by dispatch, so that a widget
    // does not each have to track the mouse itself.
    this.hovered = false;
    // Called with (this) whenever the widget's value changes. Which
    // widgets have a value, and what it is, is up to each of them.
    this.on_change = nil;
  }

  // ---- the tree ----

  // Adds children and takes ownership of them. Every container uses
  // this rather than pushing onto `children`, because a child with no
  // parent cannot bubble an event or find the application.
  add(...items) {
    for (let child in items) {
      if (child == nil) { continue; }
      child.parent = this;
      this.children.push(child);
    }
    this.invalidate();
    return this;
  }

  remove(child) {
    const at = this.children.index_of(child);
    if (at < 0) { return this; }
    this.children.remove(at);
    child.parent = nil;
    this.invalidate();
    return this;
  }

  clear() {
    for (let child in this.children) { child.parent = nil; }
    this.children.clear();
    this.invalidate();
    return this;
  }

  // The widget with this id, anywhere below here, or nil. For reaching
  // into a tree built somewhere else, which is otherwise a chain of
  // `children[0].children[2]` that breaks when the layout changes.
  find(wanted) {
    if (this.id == wanted) { return this; }
    for (let child in this.children) {
      const found = child.find(wanted);
      if (found != nil) { return found; }
    }
    return nil;
  }

  // Everything below here, depth first, this one included.
  walk(visit) {
    visit(this);
    for (let child in this.children) { child.walk(visit); }
    return this;
  }

  // The window or application at the root of the tree.
  root() {
    let at = this;
    while (at.parent != nil) { at = at.parent; }
    return at;
  }

  // ---- naming things fluently ----
  //
  // These return the widget, so that building a tree reads as one
  // expression rather than as a paragraph of assignments:
  //
  //   Column().add(
  //     Label("Name").named("caption"),
  //     Input().growing(1),
  //   )

  named(value) {
    this.id = value;
    return this;
  }

  growing(weight = 1) {
    this.flex = weight;
    return this;
  }

  padded(insets) {
    this.padding = insets;
    return this;
  }

  sized(width = nil, height = nil) {
    if (width != nil) {
      this.min_width = width;
      this.max_width = width;
    }
    if (height != nil) {
      this.min_height = height;
      this.max_height = height;
    }
    return this;
  }

  at_least(width = nil, height = nil) {
    if (width != nil) { this.min_width = width; }
    if (height != nil) { this.min_height = height; }
    return this;
  }

  at_most(width = nil, height = nil) {
    if (width != nil) { this.max_width = width; }
    if (height != nil) { this.max_height = height; }
    return this;
  }

  filled(style_name) {
    this.fill = style_name;
    return this;
  }

  shown(value = true) {
    if (this.visible != value) { this.invalidate(); }
    this.visible = value;
    return this;
  }

  // Turns a widget off: it is still drawn, greyed, and neither the
  // keyboard nor the mouse can reach it. Different from hiding, which
  // takes it out of the layout entirely.
  set_enabled(value) {
    if (this.enabled != value) { this.invalidate(); }
    this.enabled = value;
    return this;
  }

  // Say that what this shows has changed. Marks every parent too, since
  // a child's new size may change where its siblings go.
  invalidate() {
    this.dirty = true;
    let at = this.parent;
    while (at != nil and !at.dirty) {
      at.dirty = true;
      at = at.parent;
    }
    return this;
  }

  // Can the keyboard be given to this, right now?
  accepts_focus() {
    return this.focusable and this.enabled and this.visible;
  }

  // ---- measuring ----

  // How big this would like to be, given what there is. Override
  // `measure_content` rather than this: padding and the min and max
  // bounds are applied here, and every widget wants that done the same
  // way.
  measure(available) {
    if (!this.visible) { return Size(0, 0); }
    const inner = Size(max(0, available.width - this.padding.horizontal()),
                       max(0, available.height - this.padding.vertical()));
    const content = this.measure_content(inner);
    return this.bounded(Size(content.width + this.padding.horizontal(),
                             content.height + this.padding.vertical()));
  }

  // What the widget itself needs, not counting its padding. This is the
  // one to override.
  measure_content(available) {
    // A plain widget takes what its children take, stacked on top of
    // each other. Containers override this; a leaf has no children and
    // so asks for nothing.
    let size = Size(0, 0);
    for (let child in this.children) {
      size = size.union(child.measure(available));
    }
    return size;
  }

  bounded(size) {
    let width = max(size.width, this.min_width);
    let height = max(size.height, this.min_height);
    if (this.max_width != nil) { width = min(width, this.max_width); }
    if (this.max_height != nil) { height = min(height, this.max_height); }
    return Size(width, height);
  }

  // The rectangle inside the padding, in this widget's coordinates.
  // Where a widget's own drawing goes.
  inner() {
    return Rect(0, 0, this.frame.width, this.frame.height)
        .shrunk(this.padding);
  }

  // ---- arranging ----

  // Put this widget at `rect`, in the parent's coordinates, and lay out
  // whatever is inside it. Override `arrange_children`.
  arrange(rect) {
    this.frame = rect;
    this.dirty = false;
    if (!this.visible) { return this; }
    this.arrange_children(this.inner());
    return this;
  }

  // Where the children go, given the room inside the padding, in this
  // widget's coordinates.
  arrange_children(area) {
    for (let child in this.children) { child.arrange(area); }
    return this;
  }

  // ---- drawing ----

  // Paints this widget. The canvas has already been framed to this
  // widget's rectangle, so 0, 0 is its own top left corner, and nothing
  // drawn outside it will land.
  //
  // Override `draw_content`: this applies the fill and then draws the
  // children, which almost every widget wants.
  draw(surface, ui) {
    if (!this.visible or this.frame.is_empty()) { return this; }
    if (this.fill != nil) {
      surface.fill(Rect(0, 0, this.frame.width, this.frame.height), " ",
                   ui.style(this.fill));
    }
    this.draw_content(surface, ui);
    this.draw_children(surface, ui);
    return this;
  }

  draw_content(surface, ui) { return this; }

  draw_children(surface, ui) {
    for (let child in this.children) {
      if (!child.visible or child.frame.is_empty()) { continue; }
      surface.frame(child.frame, fun () { child.draw(surface, ui); });
    }
    return this;
  }

  // ---- events ----

  // Offered an event. Return true when it was dealt with and should go
  // no further.
  //
  // Keys arrive at the focused widget and travel up through its parents
  // until one takes them, which is what lets a dialog handle escape
  // without every control inside it knowing about dialogs.
  on_key_event(key, ui) { return false; }

  // A mouse event, in this widget's own coordinates, offered to the
  // topmost widget under the pointer first.
  on_mouse_event(mouse, ui) { return false; }

  // Can this widget take pasted text? Anything that answers true must
  // have an insert() that takes a string. Asked rather than discovered,
  // because a paste should reach a text field and stop at anything that
  // merely happens to have a method of that name.
  accepts_text() { return false; }

  // Called when the keyboard arrives and when it leaves.
  on_focus(ui) {
    this.invalidate();
    return this;
  }

  on_blur(ui) {
    this.invalidate();
    return this;
  }

  // Which widget under this point should get a mouse event. The
  // point is in this widget's coordinates. The last matching child wins,
  // because later children are drawn on top.
  //
  // Returns [widget, x, y] in that widget's coordinates, or nil.
  hit(x, y) {
    if (!this.visible or this.frame.is_empty()) { return nil; }
    if (x < 0 or y < 0 or x >= this.frame.width or y >= this.frame.height) {
      return nil;
    }
    for (let i in range(0, this.children.len())) {
      const child = this.children[this.children.len() - 1 - i];
      if (!child.visible) { continue; }
      const found = child.hit(x - child.frame.x, y - child.frame.y);
      if (found != nil) { return found; }
    }
    if (!this.enabled) { return nil; }
    return [this, x, y];
  }

  // Where this widget is on the screen, rather than in its parent. Used
  // for anything that has to be drawn outside its own frame: an open
  // dropdown, a menu, a tooltip.
  absolute_frame() {
    let x = 0;
    let y = 0;
    let at = this;
    while (at != nil) {
      x += at.frame.x;
      y += at.frame.y;
      at = at.parent;
    }
    return Rect(x, y, this.frame.width, this.frame.height);
  }

  // ---- overlays ----
  //
  // A widget that has to draw outside its own frame — a dropdown's open
  // list is the example — says so here, and the application draws it
  // after everything else and routes the mouse to it first. Drawing it
  // in place would put it underneath the frame of whatever contains it,
  // and clip it to that frame, which is not what an open list is for.

  has_overlay() { return false; }

  // How big the overlay wants to be, in cells.
  overlay_size() { return Size(0, 0); }

  // Draws it. `rect` is where the application decided to put it, in
  // screen coordinates, and the canvas is unframed, so this draws at
  // absolute positions.
  draw_overlay(surface, ui, rect) { return this; }

  // A mouse event over the overlay. `mouse` is in screen coordinates
  // and `rect` is where the overlay was drawn.
  on_overlay_mouse(mouse, ui, rect) { return false; }

  // Everything below here that the keyboard can reach, in the order the
  // tab key should visit them: the order they were added, depth first.
  // A container that wants a different order overrides this.
  focus_ring(into = nil) {
    if (into == nil) { into = []; }
    if (!this.visible) { return into; }
    if (this.accepts_focus()) { into.push(this); }
    if (!this.enabled) { return into; }
    for (let child in this.children) { child.focus_ring(into); }
    return into;
  }

  // Tell whoever is listening that the value changed. Widgets with a
  // value call this instead of calling the handler directly, so that
  // marking the widget dirty is never forgotten.
  changed() {
    this.invalidate();
    if (this.on_change != nil) { this.on_change(this); }
    return this;
  }

  // Set the handler and give back the widget, for building a tree in
  // one expression.
  on_changed(handler) {
    this.on_change = handler;
    return this;
  }

  str() {
    let name = type(this);
    if (this.id != nil) { name += " #${this.id}"; }
    return "<${name} ${this.frame}>";
  }
}
