// A tour of andy, in one program.
//
//   red examples/andy_demo.red                  in this terminal
//   ANDY_BACKEND=native red examples/andy_demo.red   in a window
//   ANDY_THEME=light red examples/andy_demo.red      in daylight
//
// Four pages, a menu, a status bar, and every widget the library has.
// Tab moves between things, the arrow keys move inside them, alt and a
// digit reaches a page, and ctrl+q stops.
//
// The same source runs in a terminal and in a window. Nothing below
// asks which it got.

import "andy" as andy;

// ---- the data the demo shows ----

const PEOPLE = [
  ["Ada Lovelace", "1815", "Mathematician"],
  ["Grace Hopper", "1906", "Rear admiral"],
  ["Alan Turing", "1912", "Logician"],
  ["Katherine Johnson", "1918", "Physicist"],
  ["Barbara Liskov", "1939", "Computer scientist"],
  ["Margaret Hamilton", "1936", "Engineer"],
  ["Karen Sparck Jones", "1935", "Linguist"],
];

const LANGUAGES = ["Red", "C", "Lisp", "Smalltalk", "Erlang", "Forth"];

// ---- page one: the controls ----

const status = andy.StatusBar("Ready.")
.hint("tab", "move")
.hint("alt+1..4", "page")
.hint("ctrl+q", "quit");

fun say(message) { status.set_message(message); }

const nameField = andy.Input("Ada", "your name").named("name");
const passField = andy.Input("", "secret").masked().named("pass");
const sizeChoice = andy.RadioGroup(["Small", "Medium", "Large"], "Medium")
.named("size");
const languageChoice = andy.Select(LANGUAGES, "Red").named("language");
const verbose = andy.Checkbox("Say more while working", true).named("verbose");
const darkMode = andy.Switch("Dark", true).named("dark");
const volume = andy.Slider(60, 0, 100, 5).named("volume");
const progress = andy.ProgressBar(0, 100);

nameField.on_change = fun (f) { say("Name is now ${f.get_value()}"); };
languageChoice.on_change = fun (s) { say("Chose ${s.get_value()}"); };
volume.on_change = fun (s) { say("Volume ${s.get_value()}"); };

const controls = andy.Panel("A form",
  andy.Grid(["auto", "1fr"], 2,
    andy.Label("Name"), nameField,
    andy.Label("Password"), passField,
    andy.Label("Language"), languageChoice,
    andy.Label("Size"), sizeChoice,
    andy.Label(""), verbose,
    andy.Label(""), darkMode,
    andy.Label("Volume"), volume,
    andy.Label("Progress"), progress
  ).spaced(2, 0).padded(andy.uniform(1)));

// ---- page two: the views ----

const people = andy.Table(["Name", "Born", "Work"], ["1fr", "6", "20"]);
people.aligning("left", "right", "left").with_sorting();
people.set_rows(PEOPLE);
people.on_activate = fun (view, index) {
  say("Selected ${people.get_value()[0]}");
};

const languages = andy.List(LANGUAGES);
languages.detailed(fun (name) { return str(name.len()) + " letters"; });
languages.on_activate = fun (view, index) {
  say("Opened ${view.get_value()}");
};

fun build_tree() {
  const root = andy.Node("andy");
  const lib = andy.Node("lib");
  lib.add(andy.Node("widgets.red"), andy.Node("views.red"),
    andy.Node("app.red"));
  const docs = andy.Node("docs");
  docs.add(andy.Node("andy.md"));
  root.add(lib, docs, andy.Node("README.md"));
  root.expand();
  lib.expand();
  return root;
}

const tree = andy.Tree([build_tree()]);

const views_page = andy.Row(
  andy.Panel("People", people.growing(1)).growing(3),
  andy.Column(
    andy.Panel("Languages", languages.growing(1)).growing(1),
    andy.Panel("Files", tree.growing(1)).growing(1)
  ).growing(2)
);

// ---- page three: text ----

const NOTES = "andy draws a grid of cells.\n" +
"\n" +
"A cell is one column wide and one line tall, which is what a\n" +
"terminal has, and the window backend draws on the same grid, so\n" +
"that a program written for one looks like itself in the other.\n" +
"\n" +
"Type here. The arrow keys move, ctrl+z undoes, and the view\n" +
"scrolls when the caret reaches an edge.";

const notes = andy.TextArea(NOTES);

const text_page = andy.Panel("Notes", notes.growing(1).padded(andy.uniform(1)));

// ---- page four: what the screen can do ----

fun swatches() {
  const column = andy.Column();
  column.add(andy.Label("The theme, by name:").styled("title"));
  for (let name in ["text", "muted", "accent", "selection", "error",
      "warning", "success", "info", "header", "button.focused"]) {
    column.add(andy.Label("  " + name.pad_right(18) +
        "the quick brown fox").styled(name));
  }
  column.add(andy.Separator("Borders"));
  for (let name in ["single", "rounded", "double", "thick", "ascii"]) {
    column.add(andy.Panel(name, andy.Label("  a box  "))
      .with_border(andy.style.border(name)).sized(nil, 3));
  }
  return column;
}

const about_page = andy.Panel("This screen",
  andy.Row(
    andy.Scroll(swatches()).growing(1),
    andy.Column(
      andy.Label("").named("facts").wrapping().growing(1)
    ).growing(1)
  ).padded(andy.uniform(1)));

// ---- putting it together ----

const pages = andy.Tabs()
.page("Form", controls)
.page("Views", views_page)
.page("Text", text_page)
.page("Screen", about_page);

const bar = andy.MenuBar();

const root = andy.Column(bar.sized(nil, 1), pages.growing(1),
  status.sized(nil, 1));

const window = andy.App(root, {"title": "andy"});

bar.menu("File", [
    andy.MenuItem("Open...", fun (item) {
        window.prompt("Open", "Which file?", fun (answer) {
            if (answer == nil) {
              say("Cancelled.");
            } else {
              say("Would open ${answer}");
            }
          }, "notes.txt");
      }, "ctrl+o"),
    andy.MenuItem("Save", fun (item) { window.toast("Saved", "success"); },
      "ctrl+s"),
    andy.Divider(),
    andy.MenuItem("Quit", fun (item) { window.request_close(); }, "ctrl+q"),
  ]);

bar.menu("Edit", [
    andy.MenuItem("Copy notes", fun (item) {
        if (window.copy(notes.get_value())) { window.toast("Copied"); }
        else { window.toast("This screen has no clipboard", "warning"); }
      }, "ctrl+c"),
    andy.MenuItem("Clear notes", fun (item) {
        window.confirm("Clear", "Throw away the notes?", fun (yes) {
            if (yes) {
              notes.set_value("");
              say("Cleared.");
            }
          });
      }),
  ]);

bar.menu("View", [
    andy.MenuItem("Dark", fun (item) { window.set_theme(andy.theme.DARK); }),
    andy.MenuItem("Light", fun (item) { window.set_theme(andy.theme.LIGHT); }),
    andy.MenuItem("High contrast",
      fun (item) { window.set_theme(andy.theme.CONTRAST); }),
    andy.MenuItem("Monochrome", fun (item) { window.set_theme(andy.theme.MONO); }),
  ]);

bar.menu("Help", [
    andy.MenuItem("About", fun (item) {
        window.message("andy", "A user interface library for Red. The same " +
          "program draws in a terminal and in a window.");
      }, "f1"),
  ]);

// The bar is not in the focus ring — a menu bar that the tab key walked
// into would be in the way — so its menus are reached by key instead.
window.bind("f10", fun () { window.open_menu(bar, 0); });
window.bind("ctrl+o", fun () { bar.menus[0][0].action(nil); });
window.bind("ctrl+s", fun () { bar.menus[0][1].action(nil); });
window.bind("f1", fun () { bar.menus[3][0].action(nil); });
for (let i in range(1, 5)) {
  window.bind("alt+" + str(i), fun () { pages.select(i - 1); });
}

// The progress bar fills, slowly, to show that a program can animate
// without a loop of its own: a timer marks it dirty and the application
// redraws only then.
let filled = 0;
window.every(0.15, fun () {
    filled = (filled + 2) % 101;
    progress.set_value(filled);
  });

// The facts panel is filled in once the backend is known, since what it
// has to say is about the backend.
window.on_frame = fun (w) {
  const facts = w.find("facts");
  if (facts != nil and facts.value == "") {
    const size = w.screen.size();
    facts.set_text(
      "backend: ${w.screen.name}\n" +
      "size: ${size.width} by ${size.height} cells\n" +
      "colour: ${w.screen.depth()}\n" +
      "mouse: ${w.screen.has_mouse()}\n" +
      "theme: ${w.current_theme().name}\n\n" +
      "Everything on the left is named in the theme rather than " +
      "coloured in the widget, which is what lets the View menu " +
      "recolour the whole program without a single widget knowing.");
  }
};

window.on_close = fun (w) {
  // Refuse once, to show that a program may argue about being closed.
  if (!w.asked_already) {
    w.asked_already = true;
    w.confirm("Quit", "Stop the demo?", fun (yes) {
        if (yes) { w.quit(); }
        else { w.asked_already = false; }
      });
    return false;
  }
  return true;
};
window.asked_already = false;

exit(window.run(andy.choose({"title": "andy"})));
