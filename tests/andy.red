// andy, the user interface library.
//
// Every test here runs against the headless backend: real widgets, real
// layout, real drawing, and the screen read back as text. Nothing needs
// a terminal, and none of it is a mock — only the last step, where cells
// would become escape sequences, is replaced by keeping them.

import "andy" as andy;
import "andy/geom" as geom;
import "andy/text" as text;
import "andy/color" as color;
import "andy/style" as style;
import "andy/layout" as layout;
import "andy/canvas" as canvas;
import "andy/theme" as theme;
import "andy/term" as term;
import "andy/backend" as backend;
import "andy/event" as event;

// ---- geometry ----

const r = geom.Rect(2, 1, 10, 5);
print(r);                                              // expect: 10x5+2+1
print(r.right(), r.bottom());                          // expect: 12 6
print(r.shrunk(geom.uniform(1)));                      // expect: 8x3+3+2
print(r.contains(2, 1), r.contains(12, 1));            // expect: true false
print(r.intersect(geom.Rect(8, 0, 10, 10)));           // expect: 4x5+8+1
print(r.intersect(geom.Rect(50, 0, 2, 2)).is_empty()); // expect: true
print(r.union(geom.Rect(0, 0, 1, 1)));                 // expect: 12x6+0+0
print(r.center(geom.Size(4, 1)));                      // expect: 4x1+5+3

const [top, rest] = geom.Rect(0, 0, 8, 4).split_top(1);
print(top, rest); // expect: 8x1+0+0 8x3+0+1
const [side, left] = geom.Rect(0, 0, 8, 4).split_right(3);
print(side, left); // expect: 3x4+5+0 5x4+0+0

// Splitting off more than there is gives everything, not a negative.
print(geom.Rect(0, 0, 4, 2).split_top(99)[0]); // expect: 4x2+0+0
print(geom.Size(-3, 2));                       // expect: 0x2

// ---- colour ----

print(color.rgb(255, 176, 48));                                        // expect: #ffb030
print(color.parse("#1E6FD9"), color.parse("#1e6"));                    // expect: #1e6fd9 #11ee66
print(color.parse("not a colour"));                                    // expect: nil
print(color.RED.mix(color.WHITE, 0.5));                                // expect: #e89898
print(color.WHITE.contrasting(), color.BLACK.contrasting());           // expect: #000000 #ffffff
print(color.nearest_256(color.BLACK), color.nearest_256(color.WHITE)); // expect: 16 231
print(color.nearest_16(color.rgb(255, 0, 0)));                         // expect: 9
print(color.DEFAULT.is_default(), color.RED.is_default());             // expect: true false
print(color.hsl(0, 1, 0.5));                                           // expect: #ff0000

// ---- text measured in columns ----

print(text.width("hello"), text.width("héllo"), text.width("日本語")); // expect: 5 5 6
print(text.truncate("日本語です", 5));                                   // expect: 日本
print(text.ellipsize("hello world", 8));                            // expect: hello w…
print(text.wrap("the quick brown fox", 10));                        // expect: ["the quick", "brown fox"]
print(text.wrap("supercalifragilistic", 8));                        // expect: ["supercal", "ifragili", "stic"]
print(text.center("hi", 8) + "|");                                  // expect:    hi   |
print(text.clusters("éx").len());                                  // expect: 2

// ---- styles and themes ----

const bold = style.Style(color.WHITE, color.BLUE, style.BOLD);
print(bold.has(style.BOLD), bold.has(style.ITALIC));                               // expect: true false
print(bold == style.Style(color.WHITE, color.BLUE, style.BOLD));                   // expect: true
print(bold.merge(style.Style(color.RED)).fg);                                      // expect: #d03030
print(theme.DARK.get("button.focused") == theme.DARK.get("button.focused.label")); // expect: true
print(theme.named("light").name, theme.named("nope"));                             // expect: light nil
print(theme.for_depth(color.Depth.None).name);                                     // expect: mono

// ---- the canvas ----

const surface = canvas.Canvas(12, 3);
surface.box(geom.Rect(0, 0, 12, 3), style.SINGLE);
surface.frame(geom.Rect(1, 1, 10, 1), fun () {
    surface.text(0, 0, "日本語 over the edge");
  });
print(surface.row(0)); // expect: ┌──────────┐
print(surface.row(1)); // expect: │日本語 ove│

// A cell written over the right half of a wide character takes the
// left half with it, because half of one is not a character.
surface.set(2, 1, "x");
print(surface.row(1)); // expect: │ x本語 ove│

// The difference between two frames is runs of neighbouring cells.
const before = canvas.Canvas(6, 1);
const after = before.snapshot();
after.text(2, 0, "ab");
const runs = after.diff(before);
print(runs.len(), runs[0][0], runs[0][3].join("")); // expect: 1 2 ab
print(after.diff(after).len());                     // expect: 0

// ---- layout ----

const Item = layout.Item;
print(layout.distribute(30, [Item(10), Item(10), Item(10)]));       // expect: [10, 10, 10]
print(layout.distribute(30, [Item(5, 1), Item(5, 1), Item(5, 2)])); // expect: [9, 9, 12]
// Flexible things give way in proportion: a share of too little is less.
print(layout.distribute(12, [Item(10, 1), Item(10, 1), Item(10, 1)]));
// expect: [4, 4, 4]
// Things that asked for a size rather than a share have no proportion
// to honour, so what goes is the last of them. A column too short shows
// what is at the top; a row too narrow shows the first thing whole.
print(layout.distribute(12, [Item(10), Item(10), Item(10)])); // expect: [10, 2, 0]
// A maximum pins one and its share goes to the others.
print(layout.distribute(30, [Item(5, 1, 0, 8), Item(5, 1), Item(5, 2)])); // expect: [8, 9, 13]
// A minimum holds one up and the rest give way further.
print(layout.distribute(12, [Item(10), Item(10), Item(10, 0, 8)])); // expect: [4, 0, 8]
// The sizes add up to exactly what there was, whatever the rounding.
print(layout.distribute(31, [Item(0, 1), Item(0, 1), Item(0, 1)]));    // expect: [10, 11, 10]
print(layout.columns(["10", "1fr", "auto", "2fr"], 60, [0, 0, 7, 0])); // expect: [10, 14, 7, 29]
print(layout.positions_for(layout.Justify.Between, [3, 3, 3], 15));    // expect: [0, 6, 12]

// ---- drawing a whole interface ----

const form = andy.Panel("Box",
  andy.Column(andy.Label("Name"), andy.Checkbox("Ready", true))
  .padded(geom.uniform(1)));
const picture = andy.render(form, 20, 6).split("\n");
print(picture[0]); // expect: ┌─ Box ────────────┐
print(picture[2]); // expect: │ Name             │
print(picture[3]); // expect: │ [x] Ready        │
print(picture[5]); // expect: └──────────────────┘

// Too short for everything in it, a column keeps what is at the top
// and loses what is at the bottom, which is the part nobody has read
// yet.
const squeezed = andy.render(form, 20, 5).split("\n");
print(squeezed[2]); // expect: │ Name             │
print(squeezed[3]); // expect: │                  │

// A label that wraps reports the height it will take, so what is below
// it is pushed down rather than drawn over.
const paragraph = andy.Label("one two three four five").wrapping();
print(paragraph.measure(geom.Size(10, 99))); // expect: 10x3

// ---- the application drives real widgets ----

const screen = backend.Headless(30, 7);
let pressed = 0;
const field = andy.Input("", "name").named("field");
const button = andy.Button("Go", fun (b) { pressed += 1; }).named("go");
const window = andy.App(andy.Column(field, button).padded(geom.uniform(1)),
  {"theme": theme.MONO});
window.screen = screen;
screen.start();
window.layout();
window.focus_first();

print(window.focus.id); // expect: field
screen.type("ada");
window.step();
print(field.get_value()); // expect: ada
screen.key("tab");
window.step();
print(window.focus.id); // expect: go
screen.key("enter");
window.step();
print(pressed); // expect: 1
screen.key("tab", "shift");
window.step();
print(window.focus.id); // expect: field

// A key nobody wants reaches the application's own bindings.
let quit = false;
window.bind("ctrl+q", fun () { quit = true; });
screen.key("q", "ctrl");
window.step();
print(quit); // expect: true

// A dialog takes the keyboard, answers, and gives it back.
let answer = nil;
window.confirm("Sure?", "Really?", fun (yes) { answer = yes; });
window.step();
print(window.layers.len()); // expect: 1
screen.key("escape");
window.step();
print(answer, window.layers.len()); // expect: false 0
print(window.focus.id);             // expect: field

// ---- lists, tables and trees ----

const list = andy.List(["alpha", "beta", "gamma"]);
list.select(1);
print(list.get_value()); // expect: beta
list.on_key_event(event.Key("down"), window.context());
print(list.get_value()); // expect: gamma
// Past the end it stops rather than wrapping, unless asked to wrap.
list.on_key_event(event.Key("down"), window.context());
print(list.get_value()); // expect: gamma

const table = andy.Table(["Name", "Size"], ["1fr", "6"]);
table.set_rows([["b.txt", "1200"], ["a.txt", "96"], ["c.txt", "840"]]);
table.sort_by(0);
print(table.rows[0][0]); // expect: a.txt
// A column of numbers sorts as numbers, without being told which it is.
table.sort_by(1);
print(table.rows[0][1], table.rows[2][1]); // expect: 96 1200
table.sort_by(1);
print(table.rows[0][1]); // expect: 1200

const root_node = andy.Node("src");
root_node.add(andy.Node("a.red"), andy.Node("b.red"));
const tree = andy.Tree([root_node]);
print(tree.row_count()); // expect: 1
root_node.expand();
print(tree.row_count()); // expect: 3
tree.select(1);
print(tree.get_value()); // expect: a.red

// A node whose children are worked out the first time it is opened.
let asked = 0;
const lazy = andy.Node("later").loaded_by(fun (node) {
    asked += 1;
    return [andy.Node("found")];
  });
const lazy_tree = andy.Tree([lazy]);
print(asked); // expect: 0
lazy.expand();
lazy.expand();
print(asked, lazy_tree.row_count()); // expect: 1 2

// ---- decoding what a terminal sends ----

const ESC = chr(27);

fun decoded(bytes) {
  const [events, leftover, pasting, pasted] = term.decode(bytes);
  const names = [];
  for (let one in events) { names.push(str(one)); }
  return names.join(" ");
}

print(decoded("ab"));                       // expect: a b
print(decoded(ESC + "[A" + ESC + "[D"));    // expect: up left
print(decoded(ESC + "[1;5A"));              // expect: ctrl+up
print(decoded(ESC + "[3~" + ESC + "[15~")); // expect: delete f5
print(decoded(ESC + "OP"));                 // expect: f1
print(decoded(ESC + "[Z"));                 // expect: shift+tab
print(decoded(chr(1) + chr(13)));           // expect: ctrl+a enter
print(decoded(ESC + "a"));                  // expect: alt+a
print(decoded(ESC + "[<0;10;5M"));          // expect: press(9, 4) button 1
print(decoded(ESC + "[<64;3;4M"));          // expect: wheel(2, 3) button 0
print(decoded(ESC + "[I"));                 // expect: focus in
print(decoded("日"));                        // expect: 日

// A sequence split across two reads is finished by the second. With
// `flush` false a lone escape is kept rather than guessed at, which is
// what the backend wants and why the two cannot be confused.
const [first, held, p1, t1] = term.decode(ESC + "[1", false, "", false);
print(first.len(), held.len()); // expect: 0 3
const [second, done, p2, t2] = term.decode(held + ";5A", p1, t1, false);
print(second[0]); // expect: ctrl+up

// A paste arrives whole, not as a hundred key presses.
const [pasted_events, tail, p3, t3] =
term.decode(ESC + "[200~two words" + ESC + "[201~");
print(pasted_events.len(), pasted_events[0].text); // expect: 1 two words

// Escape sequences out. The leading escape is dropped so that the test
// reads as text.
print(term.style_sequence(style.Style(color.RED), color.Depth.TrueColor).sub(1));
// expect: [0;38;2;208;48;48m
print(term.style_sequence(style.Style(color.RED), color.Depth.Indexed256).sub(1));
// expect: [0;38;5;167m
print(term.style_sequence(style.Style(color.ansi(1)), color.Depth.Ansi16).sub(1));
// expect: [0;31m
print(term.style_sequence(style.Style(color.RED, color.BLUE, style.BOLD),
    color.Depth.None).sub(1));
// expect: [0;1m
print(term.base64("hello"), term.base64("hi"), term.base64("h"));
// expect: aGVsbG8= aGk= aA==

// ---- what is installed ----

// The headless backend is always there; the other two depend on the
// machine the tests are running on, so only this one is asserted.
print(andy.backends()["headless"]); // expect: true
