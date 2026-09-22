// The widgets that show a collection: lists, tables, trees, tabs, menus
// and the bars along the edges.
//
//   import "andy/views" as views;
//
//   const files = views.List(names, fun (name) { open(name); });
//
// These are the widgets with a selection and a scroll position, and they
// all handle the keyboard the same way: the arrow keys move, page up and
// down move by a screen, home and end go to the ends, enter acts on what
// is selected, and typing a letter jumps to the next entry starting with
// it. Getting that consistent across the whole set matters more than any
// one of them.

import "andy/geom" as geom;
import "andy/event" as event;
import "andy/style" as style;
import "andy/text" as text;
import "andy/color" as color;
import "andy/layout" as layout;
import "andy/widget" as widget;
import "andy/widgets" as widgets;

const Widget = widget.Widget;
const Rect = geom.Rect;
const Size = geom.Size;

// What every scrolling, selecting view has in common: a count, a
// selected index, a first visible row, and the keys that move them.
//
// Subclasses say how many rows there are and how to draw one. They do
// not repeat the arithmetic of keeping a selection on screen, which is
// where this kind of widget usually goes wrong.
class RowView < Widget {
  init() {
    super.init();
    this.focusable = true;
    this.selected = 0;
    this.top = 0;
    this.wraps = false;
    this.show_bar = true;
    this.on_activate = nil;
    // What was typed recently, for jumping to an entry by name. Cleared
    // when it stops being added to.
    this.search = "";
    this.search_at = 0;
  }

  // How many rows there are. Overridden.
  row_count() { return 0; }

  // Draw row `index` into `area`, which is one row tall and in this
  // widget's coordinates. Overridden.
  draw_row(surface, ui, index, area, chosen) { return this; }

  // The text of a row, for jumping to it by typing. Overridden by a
  // view whose rows have text.
  row_text(index) { return ""; }

  // The rows that are not headers or anything else: the area rows are
  // drawn in, in this widget's coordinates.
  rows_area() {
    const area = this.inner();
    if (this.show_bar and this.needs_bar(area)) {
      return Rect(area.x, area.y, max(0, area.width - 1), area.height);
    }
    return area;
  }

  needs_bar(area) { return this.row_count() > area.height; }

  // Moves the selection, taking the view with it.
  select(index, notify = true) {
    const count = this.row_count();
    if (count == 0) {
      this.selected = 0;
      return this;
    }
    let next = index;
    if (this.wraps) {
      next = ((index % count) + count) % count;
    } else {
      next = geom.clamp(index, 0, count - 1);
    }
    if (next == this.selected) { return this; }
    this.selected = next;
    if (notify) { this.changed(); } else { this.invalidate(); }
    return this;
  }

  step(by) { return this.select(this.selected + by); }

  // Scrolls so the selection is visible. Called while drawing, because
  // that is when the height is known.
  reveal(height) {
    if (height <= 0) { return this; }
    if (this.selected < this.top) { this.top = this.selected; }
    if (this.selected >= this.top + height) {
      this.top = this.selected - height + 1;
    }
    this.top = geom.clamp(this.top, 0, max(0, this.row_count() - height));
    return this;
  }

  activate() {
    if (this.on_activate != nil and this.row_count() > 0) {
      this.on_activate(this, this.selected);
    }
    return this;
  }

  // Called when a row is chosen: enter, or a double click.
  on_activated(handler) {
    this.on_activate = handler;
    return this;
  }

  draw_content(surface, ui) {
    const area = this.rows_area();
    if (area.is_empty()) { return this; }
    this.reveal(area.height);
    let base = ui.style("text");
    if (!this.enabled) { base = ui.style("muted"); }
    surface.fill(area, " ", base);

    const count = this.row_count();
    for (let i in range(0, area.height)) {
      const index = this.top + i;
      if (index >= count) { break; }
      const row = Rect(area.x, area.y + i, area.width, 1);
      this.draw_row(surface, ui, index, row, index == this.selected);
    }
    if (this.show_bar) { this.draw_bar(surface, ui); }
    return this;
  }

  // The style a selected row is drawn in: loud when the view has the
  // keyboard, quiet when it does not but still holds a selection.
  selection_style(ui) {
    if (ui.is_focused(this)) { return ui.style("selection"); }
    return ui.style("selection.inactive");
  }

  draw_bar(surface, ui) {
    const area = this.inner();
    if (!this.needs_bar(area)) { return this; }
    const count = this.row_count();
    const x = area.right() - 1;
    surface.vline(x, area.y, area.height, "│", ui.style("scrollbar"));
    const size = max(1, round(area.height * area.height / count));
    const span = max(1, count - area.height);
    const at = round((area.height - size) * this.top / span);
    surface.vline(x, area.y + at, size, "█", ui.style("scrollbar.thumb"));
    return this;
  }

  // Jump to the next row whose text starts with what has been typed.
  // Typing more letters refines the search; a pause starts a new one,
  // which is what makes typing "ma" find "mark" and then typing "r"
  // find "r..." rather than "mar...".
  jump_to(character, ui) {
    if (ui.now - this.search_at > 1) { this.search = ""; }
    this.search += character.lower();
    this.search_at = ui.now;
    const count = this.row_count();
    for (let i in range(0, count)) {
      const index = (this.selected + i) % count;
      if (this.row_text(index).lower().starts_with(this.search)) {
        this.select(index);
        return true;
      }
    }
    // Nothing matched what has built up; try the new letter on its own
    // before giving up, since the most likely explanation is that the
    // user has moved on to a different entry.
    if (this.search.len() > 1) {
      this.search = character.lower();
      for (let i in range(1, count + 1)) {
        const index = (this.selected + i) % count;
        if (this.row_text(index).lower().starts_with(this.search)) {
          this.select(index);
          return true;
        }
      }
    }
    return false;
  }

  page() { return max(1, this.rows_area().height - 1); }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    if (key.matches("up") or key.matches("ctrl+p")) {
      this.step(-1);
      return true;
    }
    if (key.matches("down") or key.matches("ctrl+n")) {
      this.step(1);
      return true;
    }
    if (key.matches("pageup")) {
      this.step(-this.page());
      return true;
    }
    if (key.matches("pagedown")) {
      this.step(this.page());
      return true;
    }
    if (key.matches("home")) {
      this.select(0);
      return true;
    }
    if (key.matches("end")) {
      this.select(this.row_count() - 1);
      return true;
    }
    if (key.matches("enter")) {
      this.activate();
      return true;
    }
    if (key.is_text() and key.text != " ") {
      return this.jump_to(key.text, ui);
    }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!this.enabled) { return false; }
    if (mouse.is_wheel()) {
      const area = this.rows_area();
      this.top = geom.clamp(this.top + mouse.wheel * 3, 0,
                            max(0, this.row_count() - area.height));
      this.invalidate();
      return true;
    }
    if (!mouse.is_press() or mouse.button != 1) { return false; }
    if (ui.app != nil) { ui.app.focus_on(this); }
    const area = this.rows_area();
    const index = this.top + mouse.y - area.y;
    if (index < 0 or index >= this.row_count()) { return true; }
    // A click on the row that is already selected acts on it, which is
    // the nearest thing to a double click that works on every terminal.
    if (index == this.selected) {
      this.activate();
      return true;
    }
    this.select(index);
    return true;
  }
}

// A list of things.
//
//   views.List(["one", "two"], fun (view, index) { ... })
//
// `label` says how to turn an item into a line, and defaults to str(),
// so a list of strings needs nothing and a list of records needs one
// function.
class List < RowView {
  init(items = nil, on_activate = nil) {
    super.init();
    this.items = [];
    if (items != nil) { this.items = items; }
    this.on_activate = on_activate;
    this.label = nil;
    this.marker = "› ";
    this.show_marker = true;
    // Extra text shown at the right of a row, dimmed. A size, a date, a
    // shortcut: whatever the second column would have been in a table
    // that did not need to be one.
    this.detail = nil;
  }

  // How an item becomes a line.
  labelled(f) {
    this.label = f;
    this.invalidate();
    return this;
  }

  // What is shown at the right of each row.
  detailed(f) {
    this.detail = f;
    this.invalidate();
    return this;
  }

  set_items(items, keep_selection = true) {
    this.items = items;
    if (!keep_selection) { this.selected = 0; }
    this.selected = geom.clamp(this.selected, 0, max(0, items.len() - 1));
    this.invalidate();
    return this;
  }

  get_value() {
    if (this.selected < 0 or this.selected >= this.items.len()) { return nil; }
    return this.items[this.selected];
  }

  set_value(item) {
    const at = this.items.index_of(item);
    if (at >= 0) { this.select(at); }
    return this;
  }

  row_count() { return this.items.len(); }

  row_text(index) {
    if (this.label != nil) { return str(this.label(this.items[index])); }
    return str(this.items[index]);
  }

  measure_content(available) {
    let widest = 0;
    for (let i in range(0, this.items.len())) {
      widest = max(widest, text.width(this.row_text(i)));
    }
    if (this.show_marker) { widest += text.width(this.marker); }
    return Size(widest + 1, max(1, this.items.len()));
  }

  draw_row(surface, ui, index, area, chosen) {
    let row = ui.style("text");
    if (!this.enabled) { row = ui.style("muted"); }
    else if (chosen) { row = this.selection_style(ui); }
    surface.fill(area, " ", row);

    let x = area.x;
    if (this.show_marker) {
      if (chosen) {
        surface.text(x, area.y, this.marker, row);
      }
      x += text.width(this.marker);
    }
    let room = max(0, area.right() - x);

    let trailing = "";
    if (this.detail != nil) {
      trailing = str(this.detail(this.items[index]));
      if (trailing != "") { room = max(0, room - text.width(trailing) - 1); }
    }
    surface.text(x, area.y, text.ellipsize(this.row_text(index), room), row,
                 room);
    if (trailing != "") {
      let detail_style = ui.style("muted");
      if (chosen) { detail_style = row; }
      surface.text(area.right() - text.width(trailing), area.y, trailing,
                   detail_style);
    }
    return this;
  }
}

// A list where any number of entries can be ticked.
class CheckList < List {
  init(items = nil, on_activate = nil) {
    super.init(items, on_activate);
    this.checked = set();
    this.show_marker = false;
  }

  is_checked(item) { return this.checked.has(str(item)); }

  toggle(index) {
    if (index < 0 or index >= this.items.len()) { return this; }
    const key = str(this.items[index]);
    if (this.checked.has(key)) { this.checked.remove(key); }
    else { this.checked.add(key); }
    this.changed();
    return this;
  }

  // Everything ticked, in the order it appears.
  get_value() {
    const out = [];
    for (let item in this.items) {
      if (this.is_checked(item)) { out.push(item); }
    }
    return out;
  }

  measure_content(available) {
    const size = super.measure_content(available);
    return Size(size.width + 4, size.height);
  }

  draw_row(surface, ui, index, area, chosen) {
    let row = ui.style("text");
    if (!this.enabled) { row = ui.style("muted"); }
    else if (chosen) { row = this.selection_style(ui); }
    surface.fill(area, " ", row);
    let mark = "[ ] ";
    if (this.is_checked(this.items[index])) { mark = "[x] "; }
    surface.text(area.x, area.y, mark, row);
    const room = max(0, area.width - 4);
    surface.text(area.x + 4, area.y,
                 text.ellipsize(this.row_text(index), room), row, room);
    return this;
  }

  on_key_event(key, ui) {
    if (key.matches("space")) {
      this.toggle(this.selected);
      return true;
    }
    return super.on_key_event(key, ui);
  }

  on_mouse_event(mouse, ui) {
    const before = this.selected;
    const handled = super.on_mouse_event(mouse, ui);
    if (handled and mouse.is_press() and before == this.selected) {
      const area = this.rows_area();
      if (mouse.x - area.x < 3) { this.toggle(this.selected); }
    }
    return handled;
  }
}

// Rows and columns, with headers, alignment and sorting.
//
//   const table = views.Table(["Name", "Size"], ["1fr", "8"]);
//   table.set_rows([["notes.txt", "1.2 kB"], ["photo.png", "840 kB"]]);
//
// A row is an array of strings, or an object that `cell` turns into one,
// which is what lets a table show records without copying them into
// arrays first.
class Table < RowView {
  init(headers = nil, specs = nil, rows = nil) {
    super.init();
    this.headers = [];
    if (headers != nil) { this.headers = headers; }
    this.specs = specs;
    if (this.specs == nil) {
      this.specs = [];
      for (let one in this.headers) { this.specs.push("1fr"); }
    }
    this.rows = [];
    if (rows != nil) { this.rows = rows; }
    this.aligns = [];
    for (let one in this.headers) { this.aligns.push("left"); }
    this.show_headers = true;
    this.gap = 1;
    this.cell = nil;
    // Which column the rows are sorted by, and which way. -1 for none.
    this.sort_column = -1;
    this.sort_descending = false;
    this.sortable = false;
    this.widths = [];
    this.rule = true;
  }

  // How a row and a column number become the text of one cell.
  celled(f) {
    this.cell = f;
    this.invalidate();
    return this;
  }

  // Which columns are right aligned, by index. Numbers should be; text
  // should not.
  aligning(...alignments) {
    this.aligns = alignments;
    this.invalidate();
    return this;
  }

  // Lets the user sort by clicking a header or pressing its number.
  with_sorting(value = true) {
    this.sortable = value;
    return this;
  }

  set_rows(rows, keep_selection = true) {
    this.rows = rows;
    if (!keep_selection) { this.selected = 0; }
    this.selected = geom.clamp(this.selected, 0, max(0, rows.len() - 1));
    if (this.sort_column >= 0) { this.apply_sort(); }
    this.invalidate();
    return this;
  }

  get_value() {
    if (this.selected < 0 or this.selected >= this.rows.len()) { return nil; }
    return this.rows[this.selected];
  }

  cell_text(row, column) {
    if (this.cell != nil) { return str(this.cell(row, column)); }
    if (column < row.len()) { return str(row[column]); }
    return "";
  }

  row_count() { return this.rows.len(); }

  row_text(index) { return this.cell_text(this.rows[index], 0); }

  header_height() {
    if (!this.show_headers) { return 0; }
    if (this.rule) { return 2; }
    return 1;
  }

  rows_area() {
    const area = this.inner();
    const top = this.header_height();
    let width = area.width;
    if (this.show_bar and this.rows.len() > area.height - top) {
      width = max(0, width - 1);
    }
    return Rect(area.x, area.y + top, width, max(0, area.height - top));
  }

  needs_bar(area) {
    return this.rows.len() > area.height - this.header_height();
  }

  // The widest content in each column, which "auto" columns need.
  natural_widths() {
    const widths = [];
    for (let i in range(0, this.headers.len())) {
      widths.push(text.width(str(this.headers[i])));
    }
    for (let row in this.rows) {
      for (let c in range(0, this.headers.len())) {
        widths[c] = max(widths[c], text.width(this.cell_text(row, c)));
      }
    }
    return widths;
  }

  measure_content(available) {
    const natural = this.natural_widths();
    let width = this.gap * max(0, natural.len() - 1);
    for (let one in natural) { width += one; }
    return Size(width, this.rows.len() + this.header_height());
  }

  compute_widths(area) {
    this.widths = layout.columns(this.specs, area.width, this.natural_widths(),
                                 this.gap);
    return this.widths;
  }

  align_of(column) {
    if (column < this.aligns.len()) { return this.aligns[column]; }
    return "left";
  }

  // Lays one row of cells out across `area` with the computed widths.
  draw_cells(surface, ui, area, cells, cell_style) {
    let x = area.x;
    for (let c in range(0, this.widths.len())) {
      const width = this.widths[c];
      if (width <= 0) {
        x += width + this.gap;
        continue;
      }
      if (x >= area.right()) { break; }
      let value = "";
      if (c < cells.len()) { value = cells[c]; }
      value = text.ellipsize(value, width);
      let at = x;
      if (this.align_of(c) == "right") {
        at = x + width - text.width(value);
      } else if (this.align_of(c) == "center") {
        at = x + floor((width - text.width(value)) / 2);
      }
      surface.text(at, area.y, value, cell_style, width);
      x += width + this.gap;
    }
    return this;
  }

  draw_content(surface, ui) {
    const area = this.inner();
    if (area.is_empty()) { return this; }
    this.compute_widths(this.rows_area());
    if (this.show_headers) { this.draw_headers(surface, ui, area); }
    return super.draw_content(surface, ui);
  }

  draw_headers(surface, ui, area) {
    const header = ui.style("header");
    const row = Rect(area.x, area.y, area.width, 1);
    surface.fill(row, " ", header);
    const cells = [];
    for (let c in range(0, this.headers.len())) {
      let caption = str(this.headers[c]);
      if (c == this.sort_column) {
        if (this.sort_descending) { caption += " ▾"; } else { caption += " ▴"; }
      }
      cells.push(caption);
    }
    this.draw_cells(surface, ui, row, cells, header);
    if (this.rule) {
      surface.hline(area.x, area.y + 1, area.width, "─",
                    ui.style("separator"));
    }
    return this;
  }

  draw_row(surface, ui, index, area, chosen) {
    let cell_style = ui.style("text");
    if (!this.enabled) { cell_style = ui.style("muted"); }
    else if (chosen) { cell_style = this.selection_style(ui); }
    surface.fill(area, " ", cell_style);
    const cells = [];
    for (let c in range(0, this.headers.len())) {
      cells.push(this.cell_text(this.rows[index], c));
    }
    this.draw_cells(surface, ui, area, cells, cell_style);
    return this;
  }

  // Sorts by a column, and by the same column again to reverse it.
  sort_by(column) {
    if (column < 0 or column >= this.headers.len()) { return this; }
    if (this.sort_column == column) {
      this.sort_descending = !this.sort_descending;
    } else {
      this.sort_column = column;
      this.sort_descending = false;
    }
    this.apply_sort();
    return this;
  }

  // Compares as numbers when both cells are numbers, and as text
  // otherwise, so a column of sizes sorts as sizes and a column of names
  // sorts as names, without being told which is which.
  apply_sort() {
    const column = this.sort_column;
    const descending = this.sort_descending;
    const view = this;
    const chosen = this.get_value();
    this.rows.sort(fun (a, b) {
      const left = view.cell_text(a, column);
      const right = view.cell_text(b, column);
      const ln = num(left);
      const rn = num(right);
      let earlier = left < right;
      if (ln != nil and rn != nil) { earlier = ln < rn; }
      if (descending) { return !earlier; }
      return earlier;
    });
    if (chosen != nil) {
      const at = this.rows.index_of(chosen);
      if (at >= 0) { this.selected = at; }
    }
    this.invalidate();
    return this;
  }

  on_key_event(key, ui) {
    if (this.sortable and key.is_text()) {
      const digit = num(key.text);
      if (digit != nil and digit >= 1 and digit <= this.headers.len()) {
        this.sort_by(floor(digit) - 1);
        return true;
      }
    }
    return super.on_key_event(key, ui);
  }

  on_mouse_event(mouse, ui) {
    if (this.sortable and mouse.is_press() and this.show_headers) {
      const area = this.inner();
      if (mouse.y == area.y) {
        this.compute_widths(this.rows_area());
        let x = area.x;
        for (let c in range(0, this.widths.len())) {
          if (mouse.x >= x and mouse.x < x + this.widths[c]) {
            if (ui.app != nil) { ui.app.focus_on(this); }
            this.sort_by(c);
            return true;
          }
          x += this.widths[c] + this.gap;
        }
        return true;
      }
    }
    return super.on_mouse_event(mouse, ui);
  }
}

// One node of a tree. A node holds a label, a value of the caller's
// choosing, and its children; the Tree widget below shows them.
class Node {
  init(label, value = nil, children = nil) {
    this.label = label;
    this.value = value;
    if (this.value == nil) { this.value = label; }
    this.children = [];
    if (children != nil) {
      for (let child in children) { this.add(child); }
    }
    this.expanded = false;
    this.parent = nil;
    // Set on a node whose children have not been worked out yet, so
    // that a tree of something expensive — a filesystem, a database —
    // is only asked about the parts somebody opened.
    this.loader = nil;
  }

  add(...items) {
    for (let child in items) {
      child.parent = this;
      this.children.push(child);
    }
    return this;
  }

  // Children to be worked out when the node is first opened.
  loaded_by(f) {
    this.loader = f;
    return this;
  }

  is_leaf() { return this.children.len() == 0 and this.loader == nil; }

  expand() {
    if (this.loader != nil) {
      for (let child in this.loader(this)) { this.add(child); }
      this.loader = nil;
    }
    this.expanded = true;
    return this;
  }

  collapse() {
    this.expanded = false;
    return this;
  }

  depth() {
    let n = 0;
    let at = this.parent;
    while (at != nil) {
      n += 1;
      at = at.parent;
    }
    return n;
  }

  str() { return "Node(${this.label})"; }
}

// A tree of nodes, shown as an indented list of the ones that are open.
//
// The right arrow opens a node or moves into it, the left arrow closes
// it or moves out to its parent, which is how every tree control works
// and is worth matching exactly.
class Tree < RowView {
  init(roots = nil, on_activate = nil) {
    super.init();
    this.roots = [];
    if (roots != nil) { this.roots = roots; }
    this.on_activate = on_activate;
    this.indent = 2;
    this.flat = [];
    this.show_root_lines = true;
  }

  set_roots(roots) {
    this.roots = roots;
    this.selected = 0;
    this.invalidate();
    return this;
  }

  // The nodes that are currently visible, in order. Recomputed whenever
  // the tree is drawn or asked about, which is cheap next to drawing and
  // means nothing can go stale.
  flatten() {
    const out = [];
    // An explicit stack rather than recursion: a tree of a filesystem
    // can be deeper than a call stack is willing to be, and the order
    // is the same either way.
    const pending = [];
    for (let i in range(0, this.roots.len())) {
      pending.push(this.roots[this.roots.len() - 1 - i]);
    }
    while (pending.len() > 0) {
      const node = pending.pop();
      out.push(node);
      if (!node.expanded) { continue; }
      for (let i in range(0, node.children.len())) {
        pending.push(node.children[node.children.len() - 1 - i]);
      }
    }
    this.flat = out;
    return out;
  }

  row_count() {
    this.flatten();
    return this.flat.len();
  }

  node_at(index) {
    if (index < 0 or index >= this.flat.len()) { return nil; }
    return this.flat[index];
  }

  get_value() {
    const node = this.node_at(this.selected);
    if (node == nil) { return nil; }
    return node.value;
  }

  selected_node() { return this.node_at(this.selected); }

  row_text(index) {
    const node = this.node_at(index);
    if (node == nil) { return ""; }
    return node.label;
  }

  measure_content(available) {
    this.flatten();
    let widest = 0;
    for (let node in this.flat) {
      widest = max(widest,
                   node.depth() * this.indent + 2 + text.width(node.label));
    }
    return Size(widest, max(1, this.flat.len()));
  }

  draw_row(surface, ui, index, area, chosen) {
    const node = this.node_at(index);
    if (node == nil) { return this; }
    let row = ui.style("text");
    if (!this.enabled) { row = ui.style("muted"); }
    else if (chosen) { row = this.selection_style(ui); }
    surface.fill(area, " ", row);

    const x = area.x + node.depth() * this.indent;
    let mark = "  ";
    if (!node.is_leaf()) {
      if (node.expanded) { mark = "▾ "; } else { mark = "▸ "; }
    }
    surface.text(x, area.y, mark, row);
    const room = max(0, area.right() - x - 2);
    surface.text(x + 2, area.y, text.ellipsize(node.label, room), row, room);
    return this;
  }

  on_key_event(key, ui) {
    if (!this.enabled) { return false; }
    const node = this.selected_node();
    if (key.matches("right")) {
      if (node == nil) { return true; }
      if (!node.is_leaf() and !node.expanded) {
        node.expand();
        this.invalidate();
      } else {
        this.step(1);
      }
      return true;
    }
    if (key.matches("left")) {
      if (node == nil) { return true; }
      if (node.expanded) {
        node.collapse();
        this.invalidate();
        return true;
      }
      if (node.parent != nil) {
        this.flatten();
        const at = this.flat.index_of(node.parent);
        if (at >= 0) { this.select(at); }
      }
      return true;
    }
    if (key.matches("space")) {
      if (node != nil and !node.is_leaf()) {
        if (node.expanded) { node.collapse(); } else { node.expand(); }
        this.invalidate();
      }
      return true;
    }
    return super.on_key_event(key, ui);
  }

  on_mouse_event(mouse, ui) {
    if (mouse.is_press() and mouse.button == 1) {
      const area = this.rows_area();
      const index = this.top + mouse.y - area.y;
      const node = this.node_at(index);
      if (node != nil and !node.is_leaf()) {
        const marker = area.x + node.depth() * this.indent;
        if (mouse.x >= marker and mouse.x < marker + 2) {
          if (ui.app != nil) { ui.app.focus_on(this); }
          this.select(index);
          if (node.expanded) { node.collapse(); } else { node.expand(); }
          this.invalidate();
          return true;
        }
      }
    }
    return super.on_mouse_event(mouse, ui);
  }
}

// Several pages, one showing at a time, with a strip of titles along the
// top.
//
//   views.Tabs().page("Files", files).page("Settings", settings)
class Tabs < Widget {
  init() {
    super.init();
    this.titles = [];
    this.current = 0;
    this.focusable = true;
    this.on_change = nil;
  }

  page(title, child) {
    this.titles.push(title);
    this.add(child);
    for (let i in range(0, this.children.len())) {
      this.children[i].visible = i == this.current;
    }
    return this;
  }

  select(index) {
    const next = geom.clamp(index, 0, max(0, this.children.len() - 1));
    if (next == this.current) { return this; }
    this.current = next;
    for (let i in range(0, this.children.len())) {
      this.children[i].visible = i == this.current;
    }
    this.changed();
    return this;
  }

  get_value() {
    if (this.current < this.titles.len()) { return this.titles[this.current]; }
    return nil;
  }

  strip_height() { return 1; }

  measure_content(available) {
    let width = 0;
    for (let title in this.titles) { width += text.width(title) + 3; }
    let size = Size(width, 0);
    const inner = Size(available.width,
                       max(0, available.height - this.strip_height()));
    for (let child in this.children) { size = size.union(child.measure(inner)); }
    return Size(max(width, size.width), size.height + this.strip_height());
  }

  arrange_children(area) {
    const body = Rect(area.x, area.y + this.strip_height(), area.width,
                      max(0, area.height - this.strip_height()));
    for (let i in range(0, this.children.len())) {
      const child = this.children[i];
      child.visible = i == this.current;
      if (child.visible) { child.arrange(body); }
    }
    return this;
  }

  // Where each title starts, so that drawing and clicking agree.
  title_positions() {
    const out = [];
    let x = 0;
    for (let title in this.titles) {
      const width = text.width(title) + 2;
      out.push([x, width]);
      x += width + 1;
    }
    return out;
  }

  draw_content(surface, ui) {
    const area = this.inner();
    const strip = Rect(area.x, area.y, area.width, 1);
    surface.fill(strip, " ", ui.style("tab"));
    const places = this.title_positions();
    for (let i in range(0, this.titles.len())) {
      const [x, width] = places[i];
      if (area.x + x >= area.right()) { break; }
      let tab = ui.style("tab");
      if (i == this.current) { tab = ui.style("tab.selected"); }
      if (i == this.current and ui.is_focused(this)) {
        tab = ui.style("selection");
      }
      surface.fill(Rect(area.x + x, area.y, width, 1), " ", tab);
      surface.text(area.x + x + 1, area.y,
                   text.ellipsize(this.titles[i], max(0, width - 2)), tab);
    }
    return this;
  }

  on_key_event(key, ui) {
    if (key.matches("left") or key.matches("shift+tab")) {
      this.select(this.current - 1);
      return true;
    }
    if (key.matches("right")) {
      this.select(this.current + 1);
      return true;
    }
    // Alt and a digit reaches a page from anywhere, which is what the
    // application forwards here.
    if (key.alt) {
      const digit = num(key.key);
      if (digit != nil and digit >= 1 and digit <= this.titles.len()) {
        this.select(floor(digit) - 1);
        return true;
      }
    }
    return false;
  }

  on_mouse_event(mouse, ui) {
    if (!mouse.is_press()) { return false; }
    const area = this.inner();
    if (mouse.y != area.y) { return false; }
    const places = this.title_positions();
    for (let i in range(0, places.len())) {
      const [x, width] = places[i];
      if (mouse.x >= area.x + x and mouse.x < area.x + x + width) {
        if (ui.app != nil) { ui.app.focus_on(this); }
        this.select(i);
        return true;
      }
    }
    return false;
  }

  // The focus ring includes the page that is showing and skips the
  // others, which is what stops the tab key from walking into a page
  // nobody can see.
  focus_ring(into = nil) {
    if (into == nil) { into = []; }
    if (!this.visible) { return into; }
    into.push(this);
    if (this.current < this.children.len()) {
      this.children[this.current].focus_ring(into);
    }
    return into;
  }
}

// One entry of a menu: a label, something to do, and a shortcut to show
// beside it.
class MenuItem {
  init(label, action = nil, shortcut = "") {
    this.label = label;
    this.action = action;
    this.shortcut = shortcut;
    this.enabled = true;
    this.submenu = nil;
    // A separator is an item with no label and nothing to do.
    this.separator = label == "-";
  }

  // A submenu, which opens beside this item.
  containing(...items) {
    this.submenu = items;
    return this;
  }

  str() { return "MenuItem(${this.label})"; }
}

fun Divider() { return MenuItem("-"); }

// A menu, drawn as a box of items. Used by MenuBar below and by the
// application for a context menu.
class Menu < RowView {
  init(items = nil, on_choose = nil) {
    super.init();
    this.items = [];
    if (items != nil) { this.items = items; }
    this.on_choose = on_choose;
    this.show_bar = false;
    this.bordered = true;
  }

  row_count() { return this.items.len(); }

  row_text(index) { return this.items[index].label; }

  item_at(index) {
    if (index < 0 or index >= this.items.len()) { return nil; }
    return this.items[index];
  }

  // The size the menu wants: as wide as its widest item plus its
  // shortcut, and as tall as it has items.
  measure_content(available) {
    let widest = 0;
    for (let item in this.items) {
      let width = text.width(item.label);
      if (item.shortcut != "") { width += text.width(item.shortcut) + 3; }
      if (item.submenu != nil) { width += 2; }
      widest = max(widest, width);
    }
    let extra = 0;
    if (this.bordered) { extra = 2; }
    return Size(widest + 2 + extra, this.items.len() + extra);
  }

  rows_area() {
    const area = this.inner();
    if (!this.bordered) { return area; }
    return area.shrunk(geom.uniform(1));
  }

  // Steps over separators, which cannot be selected.
  step(by) {
    const count = this.items.len();
    if (count == 0) { return this; }
    let at = this.selected;
    for (let n in range(0, count)) {
      at = ((at + by) % count + count) % count;
      if (!this.items[at].separator) { break; }
    }
    return this.select(at);
  }

  choose() {
    const item = this.item_at(this.selected);
    if (item == nil or item.separator or !item.enabled) { return this; }
    if (this.on_choose != nil) { this.on_choose(item, this); }
    if (item.action != nil) { item.action(item); }
    return this;
  }

  activate() { return this.choose(); }

  draw_content(surface, ui) {
    const area = this.inner();
    if (this.bordered) {
      surface.fill(area, " ", ui.style("menu"));
      surface.box(area, ui.border(), ui.style("dialog.border"));
    }
    return super.draw_content(surface, ui);
  }

  draw_row(surface, ui, index, area, chosen) {
    const item = this.items[index];
    if (item.separator) {
      const rule = ui.style("separator");
      const set = ui.border();
      surface.hline(area.x, area.y, area.width, set.horizontal, rule);
      // Inside a box, the rule meets the frame, so it is joined to it
      // rather than left running into it.
      if (this.bordered) {
        surface.set(area.x - 1, area.y, set.tee_left, rule, 1);
        surface.set(area.right(), area.y, set.tee_right, rule, 1);
      }
      return this;
    }
    let row = ui.style("menu");
    if (!item.enabled) { row = ui.style("menu.disabled"); }
    else if (chosen) { row = ui.style("menu.selected"); }
    surface.fill(area, " ", row);
    let room = max(0, area.width - 1);
    if (item.shortcut != "") { room = max(0, room - text.width(item.shortcut) - 2); }
    surface.text(area.x + 1, area.y, text.ellipsize(item.label, room), row,
                 room);
    if (item.shortcut != "") {
      let hint = ui.style("menu.shortcut");
      if (chosen) { hint = row; }
      surface.text(area.right() - text.width(item.shortcut) - 1, area.y,
                   item.shortcut, hint);
    }
    if (item.submenu != nil) {
      surface.text(area.right() - 1, area.y, "▸", row);
    }
    return this;
  }

  on_key_event(key, ui) {
    if (key.matches("enter") or key.matches("space")) {
      this.choose();
      return true;
    }
    // A letter chooses the first item starting with it, rather than
    // moving to it: that is what a menu does.
    if (key.is_text()) {
      const wanted = key.text.lower();
      for (let i in range(0, this.items.len())) {
        if (this.items[i].label.lower().starts_with(wanted)) {
          this.select(i);
          this.choose();
          return true;
        }
      }
      return true;
    }
    return super.on_key_event(key, ui);
  }
}

// The strip of menu titles along the top of a window.
//
//   views.MenuBar().menu("File", [views.MenuItem("Quit", quit, "ctrl+q")])
//
// Opening one asks the application for an overlay, so the menu appears
// above the window rather than inside the bar.
class MenuBar < Widget {
  init() {
    super.init();
    this.titles = [];
    this.menus = [];
    this.open_at = -1;
    this.focusable = true;
  }

  menu(title, items) {
    this.titles.push(title);
    this.menus.push(items);
    this.invalidate();
    return this;
  }

  measure_content(available) {
    let width = 0;
    for (let title in this.titles) { width += text.width(title) + 2; }
    return Size(width, 1);
  }

  positions() {
    const out = [];
    let x = 0;
    for (let title in this.titles) {
      const width = text.width(title) + 2;
      out.push([x, width]);
      x += width;
    }
    return out;
  }

  draw_content(surface, ui) {
    const area = this.inner();
    surface.fill(area, " ", ui.style("menu"));
    const places = this.positions();
    for (let i in range(0, this.titles.len())) {
      const [x, width] = places[i];
      let tab = ui.style("menu");
      if (i == this.open_at) { tab = ui.style("menu.selected"); }
      surface.fill(Rect(area.x + x, area.y, width, 1), " ", tab);
      surface.text(area.x + x + 1, area.y, this.titles[i], tab);
    }
    return this;
  }

  // Which menu is under a column, or -1.
  menu_at(x) {
    const places = this.positions();
    for (let i in range(0, places.len())) {
      const [at, width] = places[i];
      if (x >= at and x < at + width) { return i; }
    }
    return -1;
  }

  on_mouse_event(mouse, ui) {
    if (!mouse.is_press()) { return false; }
    const area = this.inner();
    const index = this.menu_at(mouse.x - area.x);
    if (index < 0) { return false; }
    if (ui.app != nil) { ui.app.open_menu(this, index); }
    return true;
  }

  on_key_event(key, ui) {
    if (key.matches("down") or key.matches("enter")) {
      if (ui.app != nil) { ui.app.open_menu(this, max(0, this.open_at)); }
      return true;
    }
    return false;
  }
}

// The line along the bottom: a message on the left and key hints on the
// right, or the other way round.
//
//   views.StatusBar("Ready").hint("ctrl+q", "quit").hint("?", "help")
class StatusBar < Widget {
  init(message = "") {
    super.init();
    this.message = message;
    this.hints = [];
    this.fill = "status";
  }

  set_message(value) {
    if (this.message == value) { return this; }
    this.message = value;
    this.invalidate();
    return this;
  }

  hint(key, what) {
    this.hints.push([key, what]);
    this.invalidate();
    return this;
  }

  clear_hints() {
    this.hints.clear();
    this.invalidate();
    return this;
  }

  measure_content(available) { return Size(text.width(this.message), 1); }

  draw_content(surface, ui) {
    const area = this.inner();
    const base = ui.style("status");
    surface.fill(area, " ", base);

    // The hints are laid out from the right, so a long message is cut
    // rather than pushing them off the edge.
    let x = area.right();
    const key_style = ui.style("status.key");
    for (let i in range(0, this.hints.len())) {
      const [key, what] = this.hints[this.hints.len() - 1 - i];
      const width = text.width(key) + text.width(what) + 3;
      if (x - width < area.x) { break; }
      x -= width;
      surface.text(x, area.y, " " + key + " ", key_style);
      surface.text(x + text.width(key) + 2, area.y, what, base);
    }
    const room = max(0, x - area.x - 1);
    surface.text(area.x, area.y, text.ellipsize(this.message, room), base,
                 room);
    return this;
  }
}

// A message that appears for a moment and goes away. The application
// drives the clock; this only knows how to draw itself.
class Toast < Widget {
  init(message, kind = "info", seconds = 3) {
    super.init();
    this.message = message;
    this.kind = kind;
    this.until = 0;
    this.seconds = seconds;
  }

  measure_content(available) {
    return Size(text.width(this.message) + 4, 3);
  }

  draw_content(surface, ui) {
    const area = Rect(0, 0, this.frame.width, this.frame.height);
    const base = ui.style("dialog");
    surface.fill(area, " ", base);
    surface.box(area, ui.border(), ui.style(this.kind));
    surface.text(2, 1, text.ellipsize(this.message, max(0, area.width - 4)),
                 ui.style(this.kind));
    return this;
  }
}
