// URLs and the encodings that go with them.
//
//   import "std/url" as url;
//
//   const u = url.parse("https://example.com:8080/a/b?x=1&y=2#top");
//   print(u["host"]);      // example.com
//   print(u["query"]["x"]); // 1

import "std/strings" as strings;

const UNRESERVED =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_.~";

const HEX = "0123456789ABCDEF";

// Percent-encodes everything that is not unreserved. This is the strict
// form, for a value going into a path segment or a query value: "/" and
// ":" are encoded too, because in that position they are data.
fun encode(text) {
  let out = "";
  for (let i in range(0, text.len())) {
    const c = text[i];
    if (UNRESERVED.contains(c)) {
      out += c;
      continue;
    }
    const code = text.code_at(i);
    out += "%" + HEX[int(code / 16)] + HEX[code % 16];
  }
  return out;
}

// The form a whole path takes, where "/" is structure rather than data.
fun encode_path(text) {
  let out = "";
  for (let piece in text.split("/")) { out += "/" + encode(piece); }
  if (out == "") { return "/"; }
  return out.sub(1);
}

// The query-string form, where a space is "+".
fun encode_query(text) { return encode(text).replace("%20", "+"); }

fun hex_value(c) {
  const code = c.code_at(0);
  if (code >= 48 and code <= 57) { return code - 48; }
  if (code >= 65 and code <= 70) { return code - 55; }
  if (code >= 97 and code <= 102) { return code - 87; }
  return -1;
}

// Undoes either form. A "%" that is not followed by two hex digits is
// left alone rather than raising: this is usually parsing something
// arriving from outside, and refusing the whole request over one stray
// percent sign is rarely what is wanted.
fun decode(text, plus_is_space = true) {
  let out = "";
  let i = 0;
  while (i < text.len()) {
    const c = text[i];
    if (c == "+" and plus_is_space) {
      out += " ";
      i += 1;
    } else if (c == "%" and i + 2 < text.len()) {
      const high = hex_value(text[i + 1]);
      const low = hex_value(text[i + 2]);
      if (high < 0 or low < 0) {
        out += c;
        i += 1;
      } else {
        out += chr(high * 16 + low);
        i += 3;
      }
    } else {
      out += c;
      i += 1;
    }
  }
  return out;
}

// "a=1&b=2" as a map. A key with no "=" maps to "". A repeated key keeps
// the last value; parse_query_all keeps them all.
fun parse_query(text) {
  let out = {};
  if (text == "") { return out; }
  for (let pair in text.split("&")) {
    if (pair == "") { continue; }
    const [key, value] = strings.cut(pair, "=");
    out[decode(key)] = decode(value);
  }
  return out;
}

// The same, with every value as an array. For "tag=a&tag=b".
fun parse_query_all(text) {
  let out = {};
  if (text == "") { return out; }
  for (let pair in text.split("&")) {
    if (pair == "") { continue; }
    const [rawKey, rawValue] = strings.cut(pair, "=");
    const key = decode(rawKey);
    if (!out.has(key)) { out[key] = []; }
    out[key].push(decode(rawValue));
  }
  return out;
}

// A map back into "a=1&b=2", with the keys in sorted order so that the
// same map always produces the same string. An array value becomes one
// pair per element.
fun build_query(values) {
  let pairs = [];
  for (let key in values.keys().sort()) {
    const value = values[key];
    if (type(value) == "array") {
      for (let one in value) {
        pairs.push(encode_query(str(key)) + "=" + encode_query(str(one)));
      }
    } else {
      pairs.push(encode_query(str(key)) + "=" + encode_query(str(value)));
    }
  }
  return pairs.join("&");
}

// Pulls a URL apart. Everything that is not there comes back as "" or as
// an empty map, so a caller never has to test for nil before using a
// part.
//
// The map holds: scheme, user, host, port, path, query (a map),
// raw_query, fragment.
fun parse(text) {
  let rest = text;
  let out = {
    "scheme": "", "user": "", "host": "", "port": 0,
    "path": "", "raw_query": "", "query": {}, "fragment": "",
  };

  const hash = rest.find("#");
  if (hash >= 0) {
    out["fragment"] = decode(rest.sub(hash + 1), false);
    rest = rest.sub(0, hash);
  }

  const question = rest.find("?");
  if (question >= 0) {
    out["raw_query"] = rest.sub(question + 1);
    out["query"] = parse_query(out["raw_query"]);
    rest = rest.sub(0, question);
  }

  const schemeAt = rest.find("://");
  if (schemeAt >= 0) {
    out["scheme"] = rest.sub(0, schemeAt).lower();
    rest = rest.sub(schemeAt + 3);
  }

  if (out["scheme"] != "" or !rest.starts_with("/")) {
    // What is left starts with an authority: [user@]host[:port]
    const slash = rest.find("/");
    let authority = rest;
    if (slash >= 0) {
      authority = rest.sub(0, slash);
      rest = rest.sub(slash);
    } else {
      rest = "";
    }
    const at = authority.find("@");
    if (at >= 0) {
      out["user"] = authority.sub(0, at);
      authority = authority.sub(at + 1);
    }
    const [host, port] = strings.cut_last(authority, ":");
    if (port != "" and num(port) != nil) {
      out["host"] = host;
      out["port"] = int(num(port));
    } else {
      out["host"] = authority;
    }
  }

  out["path"] = rest;
  if (out["path"] == "" and out["host"] != "") { out["path"] = "/"; }

  if (out["port"] == 0) {
    if (out["scheme"] == "http") { out["port"] = 80; }
    if (out["scheme"] == "https") { out["port"] = 443; }
  }
  return out;
}

// Puts one back together. The inverse of parse() for anything parse()
// produced.
fun format(parts) {
  let out = "";
  const scheme = parts.get("scheme", "");
  const host = parts.get("host", "");
  if (scheme != "") { out += scheme + "://"; }
  const user = parts.get("user", "");
  if (user != "") { out += user + "@"; }
  out += host;

  const port = parts.get("port", 0);
  let defaultPort = 0;
  if (scheme == "http") { defaultPort = 80; }
  if (scheme == "https") { defaultPort = 443; }
  if (port != 0 and port != defaultPort) { out += ":" + str(port); }

  out += parts.get("path", "");

  let query = parts.get("raw_query", "");
  const parsed = parts.get("query", nil);
  if (query == "" and parsed != nil and parsed.len() > 0) {
    query = build_query(parsed);
  }
  if (query != "") { out += "?" + query; }

  const fragment = parts.get("fragment", "");
  if (fragment != "") { out += "#" + encode(fragment); }
  return out;
}
