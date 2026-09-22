// andy — a user interface library.
//
//   import "andy" as andy;
//
//   const root = andy.Column(
//     andy.Label("What is your name?"),
//     andy.Input("", "type here").named("name"),
//     andy.Button("Done", fun (b) { andy.stop(); }),
//   ).padded(andy.uniform(1));
//
//   andy.run(root, {"title": "Hello"});
//
// The same program draws in a terminal or in a window of its own. Which
// one it gets is decided when it starts, from what is there and from
// what it was asked for; nothing above this line knows the difference.
//
// This file is the front door. It re-exports the modules, so
// `andy.geom.Rect` and `andy.views.Table` are reachable from the one
// import, and it lifts the things a program uses constantly — the
// widgets, the containers, App, run — to the top so that they are
// written without a prefix.
//
// | Module | What is in it |
// |---|---|
// | `andy/geom` | Points, sizes, rectangles, insets. |
// | `andy/color` | Colour, and reducing it for the screen at hand. |
// | `andy/style` | A cell's two colours and its attributes; borders. |
// | `andy/text` | How wide a string is, and how to cut one to fit. |
// | `andy/event` | Key presses, the mouse, resizes. |
// | `andy/theme` | Named styles, and four themes. |
// | `andy/canvas` | The grid everything is drawn on. |
// | `andy/layout` | Measuring and arranging. |
// | `andy/widget` | What a widget is. |
// | `andy/widgets` | Containers and controls. |
// | `andy/views` | Lists, tables, trees, tabs, menus, bars. |
// | `andy/backend` | What a screen has to do; a screen that is not one. |
// | `andy/term` | The terminal backend. |
// | `andy/native` | The window backend. |
// | `andy/app` | The loop, the focus, the dialogs. |
//
// docs/andy.md is the reference and examples/andy_demo.red is a program
// that uses most of it.

import "andy/geom" as geom;
import "andy/color" as color;
import "andy/style" as style;
import "andy/text" as text;
import "andy/event" as event;
import "andy/theme" as theme;
import "andy/canvas" as canvas;
import "andy/layout" as layout;
import "andy/widget" as widget;
import "andy/widgets" as widgets;
import "andy/views" as views;
import "andy/backend" as backend;
import "andy/term" as term;
import "andy/native" as native;
import "andy/app" as app;

// ---- the application ----

const App = app.App;
const Layer = app.Layer;

// ---- widgets ----

const Widget = widget.Widget;
const Context = widget.Context;

const Row = widgets.Row;
const Column = widgets.Column;
const Stack = widgets.Stack;
const Center = widgets.Center;
const Panel = widgets.Panel;
const Grid = widgets.Grid;
const Scroll = widgets.Scroll;
const Spacer = widgets.Spacer;
const Gap = widgets.Gap;

const Label = widgets.Label;
const Title = widgets.Title;
const Separator = widgets.Separator;
const Button = widgets.Button;
const Checkbox = widgets.Checkbox;
const Switch = widgets.Switch;
const Radio = widgets.Radio;
const RadioGroup = widgets.RadioGroup;
const ProgressBar = widgets.ProgressBar;
const Slider = widgets.Slider;
const Input = widgets.Input;
const NumberInput = widgets.NumberInput;
const TextArea = widgets.TextArea;
const Select = widgets.Select;

const List = views.List;
const CheckList = views.CheckList;
const Table = views.Table;
const Tree = views.Tree;
const Node = views.Node;
const Tabs = views.Tabs;
const Menu = views.Menu;
const MenuBar = views.MenuBar;
const MenuItem = views.MenuItem;
const Divider = views.Divider;
const StatusBar = views.StatusBar;
const Toast = views.Toast;

// ---- the pieces widgets are made of ----

const Rect = geom.Rect;
const Size = geom.Size;
const Point = geom.Point;
const Insets = geom.Insets;
const uniform = geom.uniform;
const symmetric = geom.symmetric;
const clamp = geom.clamp;

const Style = style.Style;
const rgb = color.rgb;
const hsl = color.hsl;
const Theme = theme.Theme;

const Align = layout.Align;
const Justify = layout.Justify;

// The attributes, so that `andy.BOLD` works without reaching into
// `andy.style`.
const BOLD = style.BOLD;
const DIM = style.DIM;
const ITALIC = style.ITALIC;
const UNDERLINE = style.UNDERLINE;
const REVERSE = style.REVERSE;
const STRIKE = style.STRIKE;

// ---- running one ----

// The application `run()` made, so that a program which only ever has
// one can call `andy.stop()` from a button and be done with it.
let current = nil;

// The application running now, or nil.
fun app_now() { return current; }

// Builds an application around `root`, runs it, and gives back the exit
// code.
//
//   andy.run(root, {"title": "Notes", "backend": "native"})
//
// | Option | Meaning |
// |---|---|
// | `title` | The window's title, and the terminal's while it runs. |
// | `backend` | `"auto"`, `"terminal"`, `"native"` or `"headless"`. |
// | `theme` | A theme, or a name. Defaults to `$ANDY_THEME`, then dark. |
// | `cols` `rows` | The size of a new window. Ignored by the terminal. |
// | `quit` | A key that stops the program. Defaults to `ctrl+q`. |
// | `screen` | A backend to use as it stands, instead of choosing one. |
//
// `$ANDY_BACKEND` overrides `backend`, which is what lets a program be
// run in a window without being edited, and lets a test run it headless.
fun run(root, options = nil) {
  if (options == nil) { options = {}; }
  const window = App(root, options);
  current = window;

  let chosen = options.get("theme", nil);
  if (chosen != nil and type(chosen) == "string") {
    chosen = theme.named(chosen);
  }
  window.theme = chosen;

  const quit = options.get("quit", "ctrl+q");
  if (quit != nil) {
    window.bind(quit, fun () { window.request_close(); });
  }

  let screen = options.get("screen", nil);
  if (screen == nil) { screen = choose(options); }
  try {
    return window.run(screen);
  } finally {
    current = nil;
  }
}

// Stops the application `run()` started. What a Quit button calls.
fun stop(code = 0) {
  if (current != nil) { current.quit(code); }
  return code;
}

// The backend a set of options asks for.
//
// "auto" prefers the terminal, because a program started from a shell
// should draw where it was started; a window is what it falls back to
// when there is no terminal, which is what happens when it is launched
// from a desktop.
fun choose(options = nil) {
  if (options == nil) { options = {}; }
  let wanted = env("ANDY_BACKEND", nil);
  if (wanted == nil) { wanted = options.get("backend", "auto"); }
  wanted = str(wanted).lower();

  const settings = {
    "title": options.get("title", "andy"),
    "cols": options.get("cols", 100),
    "rows": options.get("rows", 30),
  };

  switch (wanted) {
    case "headless": {
      return backend.Headless(settings["cols"], settings["rows"]);
    }
    case "terminal", "term", "tui": {
      if (!term.available()) {
        throw error("no terminal to draw on", nil, "io");
      }
      return term.Terminal(settings);
    }
    case "native", "window", "gui": {
      return native.Window(settings);
    }
  }

  if (term.available()) { return term.Terminal(settings); }
  if (native.available()) { return native.Window(settings); }
  throw error("andy found nothing to draw on: no terminal, and no window " +
              "system. Set $ANDY_BACKEND to headless to run without one.",
              nil, "io");
}

// Which backends this installation can use, as a map. A program that
// wants to offer the choice asks this rather than guessing.
//
//   print(andy.backends());   // {"terminal": true, "native": false, ...}
fun backends() {
  return {
    "terminal": term.available(),
    "native": native.available(),
    "headless": true,
  };
}

// Draws `root` once at a given size and gives back what it looks like,
// as one string.
//
//   print(andy.render(root, 60, 20));
//
// For a screenshot in a README, for a test, and for looking at a layout
// without starting anything. Nothing is polled and no backend is
// started: the tree is measured, arranged, drawn and turned into text.
fun render(root, width = 80, height = 24, options = nil) {
  if (options == nil) { options = {}; }
  const screen = backend.Headless(width, height);
  const window = App(root, options);
  let chosen = options.get("theme", theme.MONO);
  if (type(chosen) == "string") { chosen = theme.named(chosen); }
  window.theme = chosen;
  window.screen = screen;
  screen.start();
  window.layout();
  window.focus_first();
  window.draw();
  screen.present(window.surface);
  return screen.frame();
}
