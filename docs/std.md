# The standard library

Red's built-in functions are listed in [docs/stdlib.md](stdlib.md): they
are the things a language has to provide, because nothing written in Red
could provide them. `lib/std` is the layer above, written in Red, for the
things a program needs anyway and should not have to write again.

```red
import "std/http" as http;
import "std/strings" as strings;
```

It is found automatically: `lib` is on the library search path whenever
`red` is run from `build/` or installed beside it. The `.red` suffix is
optional in the import, and a directory is entered through the file named
after it, so `"std/http"` and `"std/http.red"` are the same import.
When you use `red build`, all shipped Red libraries, including `std`, are
included in the standalone executable and remain available without the
library files on the destination machine. Pass `--no-stdlib` to omit unused
library modules; any modules your program imports are still included.

| Module | What it is |
|---|---|
| [`std/strings`](../lib/std/strings.red) | Cutting, splitting, wrapping, and the predicates. |
| [`std/path`](../lib/std/path.red) | File paths as text: join, clean, relative, extensions. |
| [`std/fs`](../lib/std/fs.red) | Walking trees, atomic writes, globs, temporary directories. |
| [`std/time`](../lib/std/time.red) | Stopwatches, deadlines, durations, and the two date formats a network program needs. |
| [`std/url`](../lib/std/url.red) | Parsing, formatting, and percent-encoding. |
| [`std/sync`](../lib/std/sync.red) | Mutex, Semaphore, WaitGroup, Once, Pool, timeouts. |
| [`std/bufio`](../lib/std/bufio.red) | Buffered reading and writing over a socket. |
| [`std/http`](../lib/std/http.red) | An HTTP server and client. |
| [`std/log`](../lib/std/log.red) | Levelled logging, as lines or as JSON. |
| [`std/testing`](../lib/std/testing.red) | Assertions, for tests that compute an answer. |

The older libraries — [`cli.red`](../lib/cli.red),
[`json.red`](../lib/json.red), [`crc32.red`](../lib/crc32.red) — sit
beside them and are imported by their own names, as does
[`andy`](../lib/andy), the user interface library, which has a reference
of its own in [docs/andy.md](andy.md).

## std/http

An HTTP/1.1 server and client. The server is a task per connection, which
is affordable because a task is a green thread: ten thousand connections
is ten thousand parked tasks rather than ten thousand operating system
threads. There is no pool to size and no event loop to write around.

```red
import "std/http" as http;

const app = http.Router();
app.get("/", fun (req) { return http.text("hello"); });
app.get("/users/:id", fun (req) {
  return http.json_response({"id": req.params["id"]});
});
http.serve(8080, app);
```

[`examples/http_server.red`](../examples/http_server.red) is a working
service with routing, middleware, JSON and shared state.

### Routing

A pattern is a path with two kinds of placeholder:

| Pattern | Matches | `req.params` |
|---|---|---|
| `/users/:id` | `/users/7` | `{"id": "7"}` |
| `/files/*rest` | `/files/a/b.txt` | `{"rest": "a/b.txt"}` |

`get` `post` `put` `patch` `delete` `head` `options` register a route for
one method; `any` registers one for all of them. `otherwise(handler)`
replaces the 404.

A path that matches with a method that does not is a `405`, with an
`Allow` header naming the methods that would have worked. A `HEAD` with
no route of its own is served by the `GET` route with the body dropped.

```red
app.static_files("/static", "./public");
```

serves a directory. The requested path is resolved against the root
before anything is opened, so `../` in a request cannot reach outside it.

### Middleware

A middleware takes the next handler and returns a handler. That is the
whole interface.

```red
app.use(http.recover(fun (req, e) { log.error(e.message); }));
app.use(http.logger());
app.use(http.cors("https://example.com"));
app.use(http.limit_body(1024 * 1024));
```

The first one added is the outermost, which is the order they read in.
Writing one is the same shape:

```red
fun require_token(token) {
  return fun (next) {
    return fun (req) {
      if (req.header("authorization", "") != "Bearer ${token}") {
        return http.text("Unauthorized", 401);
      }
      return next(req);
    };
  };
}
```

### Requests

| | |
|---|---|
| `method` `path` `target` `version` | |
| `query` | The query string as a map. |
| `params` | What the route captured. |
| `headers` | A `Headers`; names are case-insensitive. |
| `body` | The body, as read. |
| `remote` | The address at the other end. |
| `context` | Empty, for middleware to leave things in. |
| `header(name, fallback)` `param(name, fallback)` | |
| `json()` | The body parsed as JSON. |
| `form()` | A form-encoded body as a map. |
| `is_json()` `content_type()` `keep_alive()` | |

### Responses

| Function | Gives |
|---|---|
| `text(body, status)` | `text/plain` |
| `html(body, status)` | `text/html` |
| `json_response(value, status, indent)` | `application/json` |
| `bytes(body, type, status)` | Anything else. |
| `file(path)` | A file, with its type guessed from the name. A 404 when it is not there. |
| `redirect(location, status)` | 302 by default. |
| `no_content()` `not_found()` `bad_request()` `server_error()` | |

A response is a value, so it can be adjusted before it is returned:

```red
return http.json_response(user)
    .set("cache-control", "no-store")
    .cookie("session", id, {"http_only": true, "same_site": "Lax"});
```

A handler may also return a plain string, which becomes a `text` response.

### Serving

```red
http.serve(port, handler, options);        // runs until the program ends
const server = http.serve_async(port, handler);  // returns; stop() later
```

`handler` is a `Router` or a function from a `Request` to a `Response`.
`options` may hold `host`, `backlog`, `timeout`, `max_body` and
`on_error`.

Keep-alive, chunked request bodies, `Expect: 100-continue` and `HEAD` are
handled. A malformed request is answered with a `400` rather than a
dropped connection; an error raised by a handler becomes a `500` and is
passed to `on_error`.

### The client

```red
const response = http.get("http://example.com/api/items");
if (response.ok()) { print(response.json()); }

http.post("http://example.com/items", {"json": {"name": "thing"}});
http.post("http://example.com/form", {"form": {"q": "red"}});
http.request("PATCH", url, {"body": data, "headers": {"authorization": token}});
```

`options` may hold `headers`, `body`, `json`, `form`, `timeout` (30
seconds by default) and `follow` (how many redirects; 5 by default). A
status the server did not like is still a response — only a connection
that cannot be made or a reply that cannot be read raises.

There is no TLS. `https` raises rather than pretending: put a proxy in
front that terminates it.

Requests are tasks like anything else, so many at once is the ordinary
case:

```red
let pending = [];
for (let url in urls) { pending.push(async http.get(url)); }
const responses = await pending;
```

## std/sync

Channels are the first thing to reach for. These are for what a channel
states awkwardly.

```red
const lock = sync.Mutex();
lock.with(fun () { shared.push(item); });      // released however it ends

const limit = sync.Semaphore(8);               // at most eight at a time
limit.with(fun () { fetch(url); });

const group = sync.WaitGroup();
group.add(jobs.len());
for (let job in jobs) {
  spawn fun () { try { handle(job); } finally { group.done(); } } ();
}
group.wait();

const result = sync.Once();                    // one task sets, others wait
const pool = sync.Pool(4, handle);             // fixed workers, one queue
sync.with_timeout(5, fun () { return slow(); }, "gave up");
```

Every one of them is a channel underneath, so a task waiting on one is
parked rather than spinning.

## std/bufio

A socket hands back whatever has arrived, which is almost never the shape
a protocol wants. `bufio.Reader` keeps the remainder.

```red
const reader = bufio.Reader(client);
const line = reader.line();          // nil at the end of the stream
const body = reader.exact(length);   // raises if the stream ends first
const chunk = reader.some();         // whatever is here
const rest = reader.rest();          // everything until it closes
```

`bufio.Writer` collects small writes and sends them as one, which for a
request/response protocol is the difference between one packet and
several. `write(...)`, `line(text)`, `flush()`.

## std/strings

| | |
|---|---|
| `cut(text, sep)` `cut_last(text, sep)` | Both halves, split once. |
| `split_n(text, sep, limit)` | At most `limit` pieces. |
| `lines(text)` `words(text)` | |
| `count(text, needle)` `replace_first(text, from, to)` | |
| `without_prefix` `without_suffix` `trim_chars` | |
| `center` `title` `wrap` `indent` `ellipsis` | |
| `natural_less(a, b)` | A comparison for `sort`: `file2` before `file10`. |
| `human_bytes(n)` | `"1.5 kB"`. |
| `is_space` `is_digit` `is_alpha` `is_alnum` `is_hex` `is_blank` | |

## std/path

Text operations on something shaped like a path; nothing here touches the
file system.

| | |
|---|---|
| `join(...)` | An absolute part wins. |
| `dir(p)` `base(p)` `ext(p)` `stem(p)` `with_ext(p, ext)` | |
| `clean(p)` | Resolves `.` and `..` without looking at the disk. |
| `relative(from, to)` `contains(parent, child)` `split(p)` | |
| `is_absolute(p)` | |

## std/fs

| | |
|---|---|
| `read(path)` | Like `read_file`, but raises rather than giving `nil`. |
| `read_lines` `write_lines` `append_line` | |
| `write_atomic(path, text)` | Through a temporary file, so a reader never sees half. |
| `ensure_dir(path)` | And every directory above it. |
| `walk(root, options)` | Everything underneath. `options`: `files`, `dirs`, `skip`. |
| `with_ext(root, [".red"])` `glob(root, "*.txt")` `matches(name, pattern)` | |
| `copy_file` `remove_tree` `tree_size` `temp_dir` | |

## std/time

| | |
|---|---|
| `Stopwatch()` | `elapsed()` `stop()` `reset()` |
| `Deadline(seconds)` | `passed()` `remaining()` `wait()` |
| `duration(seconds)` | `"250ms"`, `"1m 35s"`, `"1d 1h"`. |
| `iso(seconds)` | `2026-09-21T18:30:00Z`, which sorts in time order. |
| `http_date(seconds)` | `Sun, 21 Sep 2026 18:30:00 GMT`. |
| `timed(body)` | `[result, seconds]`. |
| `SECOND` `MINUTE` `HOUR` `DAY` | |

## std/url

| | |
|---|---|
| `parse(text)` | A map: scheme, user, host, port, path, query, raw_query, fragment. |
| `format(parts)` | The inverse. |
| `encode` `encode_path` `encode_query` `decode` | |
| `parse_query` `parse_query_all` `build_query` | |

## std/log

```red
log.info("listening on ${port}");
log.error("upload failed", {"path": path, "size": size});
log.set_level(log.DEBUG);
log.set_json(true);                       // one JSON object per line
const request_log = log.logger().with({"request": id});
```

Output goes to standard error, because a program's output is its output
and its log is not. `RED_LOG` sets the default level, so a program's
logging can be turned up without editing it.

## std/testing

`red test` compares a program's output against the `// expect:` comments
in it and needs none of this. This is for the other shape of test: one
that computes an answer and reports which of twenty checks failed.

```red
import "std/testing" as t;

t.equal(add(2, 2), 4);
t.near(average(xs), 3.5);
t.contains(page, "<title>");
t.raises("io", fun () { fs.read("nope"); });
t.report();                    // prints the tally, exits non-zero on failure
```
