// HTTP, both ends.
//
//   import "std/http" as http;
//
//   const app = http.Router();
//   app.get("/", fun (req) { return http.text("hello"); });
//   app.get("/users/:id", fun (req) { return http.json({"id": req.params["id"]}); });
//   http.serve(8080, app);
//
// The server is a task per connection. That is affordable because a task
// is a green thread: ten thousand connections is ten thousand parked
// tasks, not ten thousand operating system threads. There is no pool to
// size and no event loop to write around, because the scheduler is
// already doing both. docs/concurrency.md is the long version.
//
// HTTP/1.1, with keep-alive and chunked bodies. Not HTTP/2, and not TLS:
// put it behind a proxy that terminates both when it faces the internet.

import "std/bufio" as bufio;
import "std/strings" as strings;
import "std/time" as time;
import "std/url" as url;
import "json" as json;

const VERSION = "HTTP/1.1";
// The name that goes in a Server header unless the caller replaces it.
const SERVER_NAME = "red";

const STATUS_TEXT = {
  100: "Continue", 101: "Switching Protocols",
  200: "OK", 201: "Created", 202: "Accepted", 204: "No Content",
  206: "Partial Content",
  301: "Moved Permanently", 302: "Found", 303: "See Other",
  304: "Not Modified", 307: "Temporary Redirect", 308: "Permanent Redirect",
  400: "Bad Request", 401: "Unauthorized", 403: "Forbidden",
  404: "Not Found", 405: "Method Not Allowed", 406: "Not Acceptable",
  408: "Request Timeout", 409: "Conflict", 410: "Gone",
  411: "Length Required", 413: "Payload Too Large", 414: "URI Too Long",
  415: "Unsupported Media Type", 422: "Unprocessable Content",
  429: "Too Many Requests",
  500: "Internal Server Error", 501: "Not Implemented",
  502: "Bad Gateway", 503: "Service Unavailable", 504: "Gateway Timeout",
};

fun status_text(code) { return STATUS_TEXT.get(code, "Status ${code}"); }

// A guess at the type of a file, from its name. Enough for serving a
// directory of assets; not a substitute for knowing what you are
// serving.
const CONTENT_TYPES = {
  ".html": "text/html; charset=utf-8",
  ".htm": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".json": "application/json",
  ".txt": "text/plain; charset=utf-8",
  ".md": "text/markdown; charset=utf-8",
  ".xml": "application/xml",
  ".csv": "text/csv; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".jpg": "image/jpeg",
  ".jpeg": "image/jpeg",
  ".gif": "image/gif",
  ".webp": "image/webp",
  ".ico": "image/x-icon",
  ".woff": "font/woff",
  ".woff2": "font/woff2",
  ".pdf": "application/pdf",
  ".wasm": "application/wasm",
  ".red": "text/plain; charset=utf-8",
};

fun content_type_for(name) {
  let at = -1;
  for (let i in range(0, name.len())) {
    if (name[i] == ".") { at = i; }
  }
  if (at < 0) { return "application/octet-stream"; }
  return CONTENT_TYPES.get(name.sub(at).lower(), "application/octet-stream");
}

// ---------------------------------------------------------------------
// Headers
//
// Field names are case-insensitive, so they are kept lowercased and
// looked up that way. Nothing here needs the original spelling, and
// keeping both would mean two ways to ask the same question.

class Headers {
  init(values = nil) {
    this.entries = {};
    if (values != nil) {
      for (let [name, value] in values.entries()) { this.set(name, value); }
    }
  }

  set(name, value) {
    this.entries[str(name).lower()] = str(value);
    return this;
  }

  // Adds without replacing. Repeats are joined with ", ", which is what
  // HTTP says a repeated field means, except for Set-Cookie -- so that
  // one is kept as several lines when it is written out.
  add(name, value) {
    const key = str(name).lower();
    if (!this.entries.has(key)) { return this.set(key, value); }
    if (key == "set-cookie") {
      this.entries[key] = this.entries[key] + "\n" + str(value);
      return this;
    }
    this.entries[key] = this.entries[key] + ", " + str(value);
    return this;
  }

  get(name, fallback = nil) {
    return this.entries.get(str(name).lower(), fallback);
  }

  has(name) { return this.entries.has(str(name).lower()); }
  remove(name) { this.entries.remove(str(name).lower()); return this; }
  names() { return this.entries.keys().sort(); }
  len() { return this.entries.len(); }

  // Written out in the wire form, ready to go in front of a body.
  format() {
    let out = "";
    for (let name in this.names()) {
      for (let value in this.entries[name].split("\n")) {
        out += "${name}: ${value}\r\n";
      }
    }
    return out;
  }
}

// ---------------------------------------------------------------------
// Request

class Request {
  init(method, target, version = VERSION) {
    this.method = method.upper();
    this.target = target;
    this.version = version;
    this.headers = Headers();
    this.body = "";
    // Filled in by the router when the route has :parameters in it.
    this.params = {};
    // The address at the other end, when there is one.
    this.remote = "";
    // Anything a middleware wants to hand to a handler.
    this.context = {};

    const parts = url.parse(target);
    this.path = parts["path"];
    if (this.path == "") { this.path = "/"; }
    this.path = url.decode(this.path, false);
    this.raw_query = parts["raw_query"];
    this.query = parts["query"];
  }

  header(name, fallback = nil) { return this.headers.get(name, fallback); }

  // A query parameter.
  param(name, fallback = nil) { return this.query.get(name, fallback); }

  content_type() {
    const value = this.headers.get("content-type", "");
    return strings.cut(value, ";")[0].trim().lower();
  }

  is_json() { return this.content_type() == "application/json"; }

  // The body as JSON. Raises with kind "json" when it is not.
  json() { return json.parse(this.body); }

  // A form-encoded body as a map.
  form() {
    if (this.content_type() == "application/x-www-form-urlencoded") {
      return url.parse_query(this.body);
    }
    return {};
  }

  // Should the connection stay open after this?
  keep_alive() {
    const connection = this.headers.get("connection", "").lower();
    if (connection.contains("close")) { return false; }
    if (this.version == "HTTP/1.0") { return connection.contains("keep-alive"); }
    return true;
  }

  str() { return "${this.method} ${this.target}"; }
}

// ---------------------------------------------------------------------
// Response

class Response {
  init(status = 200, body = "", headers = nil) {
    this.status = status;
    this.body = body;
    this.headers = Headers(headers);
  }

  set(name, value) { this.headers.set(name, value); return this; }
  add(name, value) { this.headers.add(name, value); return this; }
  header(name, fallback = nil) { return this.headers.get(name, fallback); }

  type(value) { return this.set("content-type", value); }

  // A cookie, with the attributes people actually set. `options` may
  // hold path, domain, max_age, expires, secure, http_only, same_site.
  cookie(name, value, options = nil) {
    let text = "${name}=${url.encode(str(value))}";
    const settings = options;
    if (settings != nil) {
      if (settings.has("path")) { text += "; Path=${settings["path"]}"; }
      if (settings.has("domain")) { text += "; Domain=${settings["domain"]}"; }
      if (settings.has("max_age")) { text += "; Max-Age=${settings["max_age"]}"; }
      if (settings.has("expires")) {
        text += "; Expires=${time.http_date(settings["expires"])}";
      }
      if (settings.get("secure", false)) { text += "; Secure"; }
      if (settings.get("http_only", false)) { text += "; HttpOnly"; }
      if (settings.has("same_site")) {
        text += "; SameSite=${settings["same_site"]}";
      }
    }
    return this.add("set-cookie", text);
  }

  // The body as JSON, if that is what it is.
  json() { return json.parse(this.body); }

  ok() { return this.status >= 200 and this.status < 300; }

  str() { return "${this.status} ${status_text(this.status)}"; }
}

// The response constructors. Most handlers return one of these rather
// than building a Response by hand.

fun text(body, status = 200) {
  return Response(status, str(body)).type("text/plain; charset=utf-8");
}

fun html(body, status = 200) {
  return Response(status, str(body)).type("text/html; charset=utf-8");
}

fun json_response(value, status = 200, indent = 0) {
  return Response(status, json.stringify(value, indent))
      .type("application/json");
}

fun bytes(body, contentType = "application/octet-stream", status = 200) {
  return Response(status, body).type(contentType);
}

fun redirect(location, status = 302) {
  return Response(status, "").set("location", location);
}

fun no_content() { return Response(204, ""); }

fun not_found(message = "Not Found") { return text(message, 404); }

fun bad_request(message = "Bad Request") { return text(message, 400); }

fun server_error(message = "Internal Server Error") {
  return text(message, 500);
}

// A file from disk, with a guessed content type. Gives a 404 when it is
// not there, so a handler can return it directly.
fun file(path, status = 200) {
  if (!is_file(path)) { return not_found(); }
  const body = read_file(path);
  return Response(status, body).type(content_type_for(path));
}

// ---------------------------------------------------------------------
// Reading a request off the wire

// Reads the body, however its length was stated. Returns nil when the
// request said nothing about a body, which is not the same as an empty
// one.
fun read_body(reader, headers, limit) {
  const encoding = headers.get("transfer-encoding", "").lower();
  if (encoding.contains("chunked")) {
    let out = "";
    for (;;) {
      const sizeLine = reader.line();
      if (sizeLine == nil) { break; }
      // The line may carry chunk extensions after a ";". Nothing here
      // uses them, but they have to be cut off before the number.
      const sizeText = strings.cut(sizeLine.trim(), ";")[0];
      const size = parse_hex(sizeText);
      if (size < 0) {
        throw error("bad chunk size '${sizeText}'", sizeText, "http");
      }
      if (size == 0) {
        // Trailers, then the final blank line.
        for (;;) {
          const trailer = reader.line();
          if (trailer == nil or trailer == "") { break; }
        }
        break;
      }
      if (out.len() + size > limit) {
        throw error("body larger than ${limit} bytes", limit, "http");
      }
      out += reader.exact(size);
      reader.line();
    }
    return out;
  }

  const length = headers.get("content-length", nil);
  if (length == nil) { return ""; }
  const size = num(length.trim());
  if (size == nil or size < 0) {
    throw error("bad Content-Length '${length}'", length, "http");
  }
  if (size > limit) {
    throw error("body larger than ${limit} bytes", limit, "http");
  }
  return reader.exact(int(size));
}

fun parse_hex(text) {
  let value = 0;
  const trimmed = text.trim();
  if (trimmed == "") { return -1; }
  for (let i in range(0, trimmed.len())) {
    const c = trimmed[i];
    if (!strings.is_hex(c)) { return -1; }
    const code = c.upper().code_at(0);
    let digit = code - 48;
    if (code >= 65) { digit = code - 55; }
    value = value * 16 + digit;
  }
  return value;
}

// One request, or nil when the connection has been closed cleanly.
fun read_request(reader, limit) {
  let line = reader.line();
  // A tolerated blank line before a request: some clients send one after
  // a previous response.
  while (line != nil and line == "") { line = reader.line(); }
  if (line == nil) { return nil; }

  const parts = strings.split_n(line, " ", 3);
  if (parts.len() < 2) {
    throw error("bad request line '${line}'", line, "http");
  }
  let version = VERSION;
  if (parts.len() >= 3) { version = parts[2].trim(); }

  const request = Request(parts[0], parts[1], version);
  read_headers(reader, request.headers);
  request.body = read_body(reader, request.headers, limit);
  return request;
}

fun read_headers(reader, headers) {
  for (;;) {
    const line = reader.line();
    if (line == nil or line == "") { return headers; }
    const [name, value] = strings.cut(line, ":");
    if (value == "" and !line.contains(":")) {
      throw error("bad header line '${line}'", line, "http");
    }
    headers.add(name.trim(), value.trim());
  }
}

// ---------------------------------------------------------------------
// Routing

// One route: a method, a pattern, and what to call.
//
// A pattern is a path with two kinds of placeholder:
//
//   /users/:id        matches one segment, and names it "id"
//   /files/*rest      matches everything left, and names it "rest"
class Route {
  init(method, pattern, handler) {
    this.method = method.upper();
    this.pattern = pattern;
    this.handler = handler;
    this.segments = split_path(pattern);
  }

  // The captured parameters when the path fits this pattern, nil when it
  // does not. The method is a separate question, because a path that
  // fits but a method that does not is a 405 rather than a 404, and the
  // answer has to name the methods that would have worked.
  match_path(path) {
    const parts = split_path(path);
    let params = {};
    let i = 0;
    while (i < this.segments.len()) {
      const segment = this.segments[i];
      if (segment.starts_with("*")) {
        params[segment.sub(1)] = parts.slice(i).join("/");
        return params;
      }
      if (i >= parts.len()) { return nil; }
      if (segment.starts_with(":")) {
        params[segment.sub(1)] = parts[i];
      } else if (segment != parts[i]) {
        return nil;
      }
      i += 1;
    }
    if (parts.len() != this.segments.len()) { return nil; }
    return params;
  }

  accepts(method) { return this.method == "*" or this.method == method; }
}

fun split_path(path) {
  let out = [];
  for (let piece in path.split("/")) {
    if (piece != "") { out.push(piece); }
  }
  return out;
}

// Sends a request to the first route that matches.
//
//   const app = http.Router();
//   app.get("/health", fun (req) { return http.text("ok"); });
//   app.post("/items", createItem);
//   http.serve(8080, app);
//
// Middleware wraps everything registered, innermost first:
//
//   app.use(http.logger());
class Router {
  init() {
    this.routes = [];
    this.middleware = [];
    this.fallback = fun (req) { return not_found(); };
  }

  route(method, pattern, handler) {
    this.routes.push(Route(method, pattern, handler));
    return this;
  }

  get(pattern, handler) { return this.route("GET", pattern, handler); }
  post(pattern, handler) { return this.route("POST", pattern, handler); }
  put(pattern, handler) { return this.route("PUT", pattern, handler); }
  patch(pattern, handler) { return this.route("PATCH", pattern, handler); }
  delete(pattern, handler) { return this.route("DELETE", pattern, handler); }
  head(pattern, handler) { return this.route("HEAD", pattern, handler); }
  options(pattern, handler) { return this.route("OPTIONS", pattern, handler); }
  // Any method.
  any(pattern, handler) { return this.route("*", pattern, handler); }

  // What to call when nothing matches. The default is a 404.
  otherwise(handler) {
    this.fallback = handler;
    return this;
  }

  // Serves a directory under a prefix. The path is checked against the
  // root after both are resolved, so "../" in a request cannot reach
  // outside it.
  static_files(prefix, root, index = "index.html") {
    const base = prefix;
    const directory = root;
    const indexName = index;
    return this.get(join_pattern(prefix, "*rest"), fun (req) {
      return serve_file_under(directory, req.params.get("rest", ""), indexName);
    });
  }

  use(middleware) {
    this.middleware.push(middleware);
    return this;
  }

  // Finds the route and calls it. This is the handler shape serve()
  // wants, so a Router can be passed straight to it.
  handle(request) {
    let called = fun (req) {
      let allowed = [];
      // A HEAD with no route of its own is served by the GET route, with
      // the body dropped on the way out. Every client expects that, and
      // writing each route twice to provide it would be silly.
      let headFallback = nil;

      for (let route in this.routes) {
        const params = route.match_path(req.path);
        if (params == nil) { continue; }
        if (route.accepts(req.method)) {
          req.params = params;
          return route.handler(req);
        }
        if (req.method == "HEAD" and route.accepts("GET") and
            headFallback == nil) {
          headFallback = [route, params];
        }
        if (!allowed.contains(route.method)) { allowed.push(route.method); }
      }

      if (headFallback != nil) {
        req.params = headFallback[1];
        return headFallback[0].handler(req);
      }
      if (allowed.len() > 0) {
        if (allowed.contains("GET") and !allowed.contains("HEAD")) {
          allowed.push("HEAD");
        }
        return text("Method Not Allowed", 405)
            .set("allow", allowed.sort().join(", "));
      }
      return this.fallback(req);
    };

    // Applied in reverse so that the first one added is the outermost,
    // which is the order they read in.
    let i = this.middleware.len() - 1;
    while (i >= 0) {
      called = this.middleware[i](called);
      i -= 1;
    }
    return called(request);
  }
}

fun join_pattern(prefix, tail) {
  if (prefix.ends_with("/")) { return prefix + tail; }
  return prefix + "/" + tail;
}

fun serve_file_under(root, relative, index) {
  // Rebuilt from its segments with "." and ".." resolved, and refused if
  // it ever climbs above the root. Checked here rather than left to the
  // file system, because a symbolic link would otherwise decide it.
  let parts = [];
  for (let piece in relative.split("/")) {
    if (piece == "" or piece == ".") { continue; }
    if (piece == "..") {
      if (parts.len() == 0) { return not_found(); }
      parts.pop();
      continue;
    }
    parts.push(piece);
  }
  let path = root;
  if (parts.len() > 0) { path = root + "/" + parts.join("/"); }
  if (is_dir(path)) { path = path + "/" + index; }
  return file(path);
}

// ---------------------------------------------------------------------
// Middleware
//
// A middleware takes the next handler and returns a handler. Nothing
// more: everything else is a matter of what it does before and after
// calling what it was given.

// Logs one line per request, after the response is known.
fun logger(write = print) {
  return fun (next) {
    return fun (req) {
      const started = time.monotonic();
      const response = next(req);
      const took = time.duration(time.monotonic() - started);
      write("${req.method} ${req.target} ${response.status} ${took}");
      return response;
    };
  };
}

// Turns an error raised by a handler into a 500 rather than a dropped
// connection, and hands it to `report` so it is not lost.
fun recover(report = nil) {
  return fun (next) {
    return fun (req) {
      try {
        return next(req);
      } catch (e) {
        if (report != nil) { report(req, e); }
        return server_error();
      }
    };
  };
}

// The headers a browser wants before it will let a page call this from
// another origin.
fun cors(origin = "*", methods = "GET, POST, PUT, PATCH, DELETE, OPTIONS") {
  return fun (next) {
    return fun (req) {
      if (req.method == "OPTIONS") {
        return no_content()
            .set("access-control-allow-origin", origin)
            .set("access-control-allow-methods", methods)
            .set("access-control-allow-headers", "content-type, authorization")
            .set("access-control-max-age", "86400");
      }
      const response = next(req);
      return response.set("access-control-allow-origin", origin);
    };
  };
}

// Refuses a body larger than a limit before the handler sees it.
fun limit_body(maximum) {
  return fun (next) {
    return fun (req) {
      if (req.body.len() > maximum) {
        return text("Payload Too Large", 413);
      }
      return next(req);
    };
  };
}

// ---------------------------------------------------------------------
// The server

// Writes a response out.
fun write_response(writer, response, request, keepAlive) {
  let body = response.body;
  const head = request != nil and request.method == "HEAD";

  writer.line("${VERSION} ${response.status} ${status_text(response.status)}");

  if (!response.headers.has("date")) {
    response.headers.set("date", time.http_date());
  }
  if (!response.headers.has("server")) {
    response.headers.set("server", SERVER_NAME);
  }
  if (!response.headers.has("content-type") and body.len() > 0) {
    response.headers.set("content-type", "application/octet-stream");
  }
  // A 204 and a 304 carry no body, and saying they do confuses clients
  // into waiting for one.
  if (response.status == 204 or response.status == 304) { body = ""; }
  response.headers.set("content-length", str(body.len()));
  if (keepAlive) {
    response.headers.set("connection", "keep-alive");
  } else {
    response.headers.set("connection", "close");
  }

  writer.write(response.headers.format());
  writer.line();
  if (!head) { writer.write(body); }
  writer.flush();
}

// A running server. `serve()` makes one and runs it; this is what to use
// when the program needs to stop it again.
class Server {
  init(port, handler, options = nil) {
    const settings = options;
    let backlog = 128;
    let host = "0.0.0.0";
    this.timeout = 30;
    this.max_body = 8 * 1024 * 1024;
    this.on_error = nil;
    if (settings != nil) {
      backlog = settings.get("backlog", backlog);
      host = settings.get("host", host);
      this.timeout = settings.get("timeout", this.timeout);
      this.max_body = settings.get("max_body", this.max_body);
      this.on_error = settings.get("on_error", nil);
    }
    this.handler = handler;
    this.socket = tcp_listen(port, backlog, host);
    this.running = true;
    this.connections = 0;
  }

  port() { return this.socket.port(); }

  // Accepts until stop() is called. One task per connection.
  run() {
    for (;;) {
      if (!this.running) { break; }
      let client = nil;
      try {
        client = this.socket.accept();
      } catch (e) {
        // A closed listening socket is how stop() gets out of here, and
        // is not worth reporting.
        if (!this.running) { break; }
        throw e;
      }
      client.set_timeout(this.timeout);
      const server = this;
      spawn fun () { server.serve_connection(client); } ();
    }
    return this;
  }

  // One connection, for as long as it stays open.
  serve_connection(client) {
    this.connections += 1;
    const reader = bufio.Reader(client);
    const writer = bufio.Writer(client);
    try {
      for (;;) {
        let request = nil;
        try {
          request = read_request(reader, this.max_body);
        } catch (e: "http") {
          write_response(writer, bad_request(e.message), nil, false);
          break;
        } catch (e: "bufio") {
          write_response(writer, bad_request(e.message), nil, false);
          break;
        } catch (e: "timeout") {
          break;
        } catch (e: "net") {
          break;
        }
        if (request == nil) { break; }
        request.remote = client.peer();

        // A client that announced it is waiting for permission gets it.
        // Refusing would mean it never sends the body it already told us
        // the length of.
        if (request.headers.get("expect", "").lower() == "100-continue") {
          client.write("${VERSION} 100 Continue\r\n\r\n");
        }

        let response = nil;
        try {
          response = this.handler(request);
        } catch (e) {
          if (this.on_error != nil) { this.on_error(request, e); }
          response = server_error();
        }
        if (response == nil) { response = no_content(); }
        if (type(response) == "string") { response = text(response); }

        const keepAlive = this.running and request.keep_alive();
        write_response(writer, response, request, keepAlive);
        if (!keepAlive) { break; }
      }
    } catch (e) {
      // The peer went away mid-response, most likely. There is nobody
      // left to tell.
      if (this.on_error != nil) { this.on_error(nil, e); }
    } finally {
      client.close();
      this.connections -= 1;
    }
  }

  stop() {
    this.running = false;
    this.socket.close();
    return this;
  }
}

// Serves until the program ends.
//
//   http.serve(8080, app);
//
// `handler` is either a function taking a Request and returning a
// Response, or anything with a `handle` method -- which is what makes a
// Router work here directly.
fun serve(port, handler, options = nil) {
  const server = Server(port, as_handler(handler), options);
  server.run();
  return server;
}

// Starts one on its own task and gives back the Server, so the caller
// can carry on and stop it later. This is what a test does.
fun serve_async(port, handler, options = nil) {
  const server = Server(port, as_handler(handler), options);
  spawn fun () { server.run(); } ();
  return server;
}

fun as_handler(handler) {
  if (type(handler) == "instance") {
    const router = handler;
    return fun (req) { return router.handle(req); };
  }
  return handler;
}

// ---------------------------------------------------------------------
// The client

// One request. `options` may hold:
//
//   headers  a map of header fields
//   body     a string, sent as it is
//   json     a value, encoded and sent as application/json
//   form     a map, encoded as application/x-www-form-urlencoded
//   timeout  seconds; 30 by default
//   follow   how many redirects to follow; 5 by default
//
// Returns a Response. A status the server did not like is still a
// response, not an error: only a connection that could not be made or a
// reply that could not be read raises.
fun request(method, target, options = nil) {
  let settings = options;
  if (settings == nil) { settings = {}; }
  let follow = settings.get("follow", 5);
  let at = target;

  for (;;) {
    const response = request_once(method, at, settings);
    if (follow <= 0) { return response; }
    const moved = response.status == 301 or response.status == 302 or
                  response.status == 303 or response.status == 307 or
                  response.status == 308;
    if (!moved) { return response; }
    const location = response.header("location", nil);
    if (location == nil) { return response; }
    at = absolute_location(at, location);
    // A 303, and in practice a 301 or 302, turns the next request into a
    // GET. A 307 and a 308 exist precisely to say "do not".
    if (response.status != 307 and response.status != 308) {
      method = "GET";
      settings = copy_without_body(settings);
    }
    follow -= 1;
  }
}

fun copy_without_body(settings) {
  let out = {};
  for (let [key, value] in settings.entries()) {
    if (key == "body" or key == "json" or key == "form") { continue; }
    out[key] = value;
  }
  return out;
}

fun absolute_location(from, location) {
  if (location.contains("://")) { return location; }
  const base = url.parse(from);
  if (location.starts_with("/")) {
    base["path"] = location;
    base["raw_query"] = "";
    base["query"] = {};
    return url.format(base);
  }
  let directory = base["path"];
  let at = -1;
  for (let i in range(0, directory.len())) {
    if (directory[i] == "/") { at = i; }
  }
  if (at >= 0) { directory = directory.sub(0, at + 1); }
  base["path"] = directory + location;
  base["raw_query"] = "";
  base["query"] = {};
  return url.format(base);
}

fun request_once(method, target, settings) {
  const parts = url.parse(target);
  if (parts["host"] == "") {
    throw error("no host in '${target}'", target, "http");
  }
  if (parts["scheme"] == "https") {
    throw error(
        "https is not supported: put a proxy in front, or use http",
        target, "http");
  }

  let body = settings.get("body", "");
  let headers = Headers(settings.get("headers", nil));

  if (settings.has("json")) {
    body = json.stringify(settings["json"]);
    if (!headers.has("content-type")) {
      headers.set("content-type", "application/json");
    }
  } else if (settings.has("form")) {
    body = url.build_query(settings["form"]);
    if (!headers.has("content-type")) {
      headers.set("content-type", "application/x-www-form-urlencoded");
    }
  }

  const timeout = settings.get("timeout", 30);
  let port = parts["port"];
  if (port == 0) { port = 80; }

  let hostHeader = parts["host"];
  if (port != 80) { hostHeader = "${parts["host"]}:${port}"; }
  headers.set("host", hostHeader);
  headers.set("connection", "close");
  if (!headers.has("accept")) { headers.set("accept", "*/*"); }
  if (!headers.has("user-agent")) { headers.set("user-agent", SERVER_NAME); }
  if (body.len() > 0 or method.upper() == "POST" or method.upper() == "PUT") {
    headers.set("content-length", str(body.len()));
  }

  let requestTarget = parts["path"];
  if (requestTarget == "") { requestTarget = "/"; }
  if (parts["raw_query"] != "") {
    requestTarget += "?" + parts["raw_query"];
  }

  const socket = tcp_connect(parts["host"], port, timeout);
  socket.set_timeout(timeout);
  try {
    const writer = bufio.Writer(socket);
    writer.line("${method.upper()} ${requestTarget} ${VERSION}");
    writer.write(headers.format());
    writer.line();
    if (body.len() > 0) { writer.write(body); }
    writer.flush();
    return read_response(bufio.Reader(socket), method);
  } finally {
    socket.close();
  }
}

fun read_response(reader, method) {
  const line = reader.line();
  if (line == nil) {
    throw error("the server closed the connection without answering", nil,
                "http");
  }
  const parts = strings.split_n(line, " ", 3);
  if (parts.len() < 2 or num(parts[1]) == nil) {
    throw error("bad status line '${line}'", line, "http");
  }
  const status = int(num(parts[1]));

  const response = Response(status);
  read_headers(reader, response.headers);

  // A HEAD, a 204 and a 304 have no body however their headers read.
  if (method.upper() == "HEAD" or status == 204 or status == 304) {
    return response;
  }

  const encoding = response.headers.get("transfer-encoding", "").lower();
  const length = response.headers.get("content-length", nil);
  if (encoding.contains("chunked") or length != nil) {
    response.body = read_body(reader, response.headers, 1024 * 1024 * 64);
  } else {
    // Neither: the body is whatever arrives until the connection closes,
    // which is what "Connection: close" means.
    response.body = reader.rest();
  }
  return response;
}

fun get(target, options = nil) { return request("GET", target, options); }
fun head(target, options = nil) { return request("HEAD", target, options); }
fun delete(target, options = nil) { return request("DELETE", target, options); }

fun post(target, options = nil) { return request("POST", target, options); }
fun put(target, options = nil) { return request("PUT", target, options); }
fun patch(target, options = nil) { return request("PATCH", target, options); }
