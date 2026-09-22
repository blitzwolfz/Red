// A small web service.
//
//   ./build/red examples/http_server.red
//   ./build/red examples/http_server.red --port 9000
//
// Then:
//
//   curl localhost:8080/
//   curl localhost:8080/notes
//   curl -X POST localhost:8080/notes -d '{"text":"remember this"}'
//   curl localhost:8080/notes/1
//
// The whole server is a task per connection. There is no pool to size
// and no event loop to write around: `spawn` is cheap because a task is
// a green thread, and everything that waits parks a task rather than a
// thread. docs/concurrency.md explains what is underneath.

import "std/http" as http;
import "std/log" as log;
import "std/sync" as sync;
import "cli.red" as cli;

const spec = cli.Spec("http_server", "A small web service, to show what std/http does.");
spec.option("port", "p", "8080", "port to listen on");
spec.flag("quiet", "q", "do not log requests");

let options = nil;
try {
  options = spec.parse(args());
} catch (e: "usage") {
  print(e.message);
  print(spec.usage());
  exit(64);
}
if (options.flag("help")) { print(spec.usage()); exit(0); }

const port = int(num(options.option("port")));

// The notes live in memory. One lock around them, because every request
// runs on its own task and they all reach the same map.
const guard = sync.Mutex();
let notes = {};
let nextId = 1;

fun addNote(text) {
  return guard.with(fun () {
    const id = nextId;
    nextId += 1;
    notes[id] = {"id": id, "text": text, "at": int(time())};
    return notes[id];
  });
}

fun allNotes() {
  return guard.with(fun () {
    let out = [];
    for (let id in notes.keys().sort()) { out.push(notes[id]); }
    return out;
  });
}

fun findNote(id) {
  return guard.with(fun () { return notes.get(id, nil); });
}

const app = http.Router();

// Middleware wraps every route. The first one added is the outermost, so
// recover() is what catches anything the others let through.
app.use(http.recover(fun (req, e) {
  log.error("handler failed", {"path": req.path, "error": e.message});
}));
if (!options.flag("quiet")) {
  app.use(http.logger(fun (line) { log.info(line); }));
}

app.get("/", fun (req) {
  return http.html(
      "<h1>Notes</h1>" +
      "<p>GET /notes, POST /notes, GET /notes/:id, GET /slow</p>");
});

app.get("/health", fun (req) {
  const info = sched_info();
  return http.json_response({
    "status": "ok",
    "notes": allNotes().len(),
    "workers": info["workers"],
    "tasks": info["alive"],
  });
});

app.get("/notes", fun (req) {
  return http.json_response(allNotes(), 200, 2);
});

app.post("/notes", fun (req) {
  if (req.body.trim() == "") { return http.bad_request("empty body"); }
  let text = req.body;
  if (req.is_json()) {
    const sent = req.json();
    text = sent.get("text", "");
  }
  if (text == "") { return http.bad_request("a note needs some text"); }
  const note = addNote(text);
  return http.json_response(note, 201)
      .set("location", "/notes/${note["id"]}");
});

app.get("/notes/:id", fun (req) {
  const id = num(req.params["id"]);
  if (id == nil) { return http.bad_request("that is not an id"); }
  const note = findNote(int(id));
  if (note == nil) { return http.not_found("no note ${int(id)}"); }
  return http.json_response(note);
});

// Shows what the scheduler is for: this request waits a second, and the
// server answers every other request while it does. Try twenty at once.
app.get("/slow", fun (req) {
  sleep(1);
  return http.text("that took a second, and nothing else waited");
});

app.static_files("/static", source_dir() + "/logstat");

addNote("the first note");

log.info("listening on http://localhost:${port}");
http.serve(port, app);
