// The HTTP server and client in lib/std/http.red, talking to each other.

import "std/http" as http;
import "std/bufio" as bufio;

// ---- pieces ---------------------------------------------------------

print(http.status_text(404));      // expect: Not Found
print(http.status_text(599));      // expect: Status 599
print(http.content_type_for("page.html"));  // expect: text/html; charset=utf-8
print(http.content_type_for("blob"));       // expect: application/octet-stream

const headers = http.Headers();
headers.set("Content-Type", "text/plain");
headers.add("X-Tag", "one");
headers.add("X-Tag", "two");
print(headers.get("content-type"));   // expect: text/plain
print(headers.get("x-tag"));          // expect: one, two
print(headers.has("X-TAG"));          // expect: true
print(headers.get("missing", "fallback"));  // expect: fallback

const request = http.Request("get", "/search?q=red%20lang&n=2");
print(request.method);                // expect: GET
print(request.path);                  // expect: /search
print(request.param("q"));            // expect: red lang
print(request.param("missing", "-")); // expect: -

// ---- routing --------------------------------------------------------

const route = http.Route("GET", "/users/:id/posts/:post", nil);
print(route.match_path("/users/7/posts/hello"));
// expect: {"id": "7", "post": "hello"}
print(route.match_path("/users/7") == nil);     // expect: true

const wild = http.Route("GET", "/files/*rest", nil);
print(wild.match_path("/files/a/b/c.txt"));     // expect: {"rest": "a/b/c.txt"}

// ---- a server and a client ------------------------------------------

const app = http.Router();
app.use(http.recover());
app.get("/", fun (req) { return http.text("root"); });
app.get("/json", fun (req) { return http.json_response({"n": 1, "ok": true}); });
app.get("/users/:id", fun (req) { return http.text(req.params["id"]); });
app.get("/files/*rest", fun (req) { return http.text(req.params["rest"]); });
app.post("/echo", fun (req) {
  return http.text("${req.method} ${req.header("content-type", "-")} ${req.body}");
});
app.post("/form", fun (req) { return http.text(req.form().get("name", "-")); });
app.put("/only-put", fun (req) { return http.text("put"); });
app.get("/moved", fun (req) { return http.redirect("/"); });
app.get("/boom", fun (req) { throw "deliberate"; });
app.get("/empty", fun (req) { return http.no_content(); });
app.get("/slow", fun (req) { sleep(0.05); return http.text("late"); });

const server = http.serve_async(0, app);
const base = "http://127.0.0.1:${server.port()}";

print(http.get("${base}/").body);            // expect: root
print(http.get("${base}/").status);          // expect: 200
print(http.get("${base}/json").body);        // expect: {"n":1,"ok":true}
print(http.get("${base}/json").json()["n"]); // expect: 1
print(http.get("${base}/users/42").body);    // expect: 42
print(http.get("${base}/files/a/b.txt").body); // expect: a/b.txt
print(http.get("${base}/nowhere").status);   // expect: 404
print(http.get("${base}/boom").status);      // expect: 500
print(http.get("${base}/empty").status);     // expect: 204
print(http.get("${base}/empty").body == ""); // expect: true

// A HEAD is answered by the GET route, with the body left off.
const headResponse = http.head("${base}/");
print(headResponse.status);                  // expect: 200
print(headResponse.body == "");              // expect: true

// A path that exists with a different method says which would work.
const wrongMethod = http.get("${base}/only-put");
print(wrongMethod.status);                   // expect: 405
print(wrongMethod.header("allow"));          // expect: PUT

// Bodies, in the three shapes the client can send them.
print(http.post("${base}/echo", {"body": "plain"}).body);
// expect: POST - plain
print(http.post("${base}/echo", {"json": {"a": 1}}).body);
// expect: POST application/json {"a":1}
print(http.post("${base}/form", {"form": {"name": "ada"}}).body);
// expect: ada

// A redirect is followed by default, and not when asked not to.
print(http.get("${base}/moved").body);                     // expect: root
print(http.get("${base}/moved", {"follow": 0}).status);    // expect: 302

// Content-Length is set, and a response knows whether it went well.
const root = http.get("${base}/");
print(root.header("content-length"));        // expect: 4
print(root.ok());                            // expect: true
print(http.get("${base}/nowhere").ok());     // expect: false

// Keep-alive: two requests down one connection.
const reused = tcp_connect("127.0.0.1", server.port(), 5);
reused.set_timeout(5);
reused.write("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
const reader = bufio.Reader(reused);
print(reader.line());                        // expect: HTTP/1.1 200 OK
let sawKeepAlive = false;
for (;;) {
  const line = reader.line();
  if (line == nil or line == "") { break; }
  if (line.lower() == "connection: keep-alive") { sawKeepAlive = true; }
}
print(sawKeepAlive);                         // expect: true
print(reader.exact(4));                      // expect: root
reused.write("GET /users/9 HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
print(reader.line());                        // expect: HTTP/1.1 200 OK
reused.close();

// A malformed request is a 400, not a dropped connection.
const rude = tcp_connect("127.0.0.1", server.port(), 5);
rude.set_timeout(5);
rude.write("NOT-A-REQUEST\r\n\r\n");
print(rude.read().split(" ")[1]);            // expect: 400
rude.close();

// Many at once. Each takes 50ms on the server; together they take about
// as long as one, because none of them holds a thread while it waits.
let waiting = [];
for (let i in range(0, 30)) { waiting.push(async http.get("${base}/slow")); }
let fine = 0;
for (let response in await waiting) {
  if (response.body == "late") { fine += 1; }
}
print(fine);                                 // expect: 30

server.stop();
