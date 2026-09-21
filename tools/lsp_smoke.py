#!/usr/bin/env python3
"""Drives tools/red-lsp.red through one session and checks the answers.

    python3 tools/lsp_smoke.py ./build/red tools/red-lsp.red

Written in Python rather than in Red because it has to be the thing on
the other end of the pipe, and a Red program testing a Red program over
its own stdin would be harder to read, not easier.
"""
import json
import re
import subprocess
import sys


def frame(message):
    body = json.dumps(message).encode()
    return b"Content-Length: %d\r\n\r\n" % len(body) + body


def parse(data):
    out = []
    at = 0
    while at < len(data):
        header = re.match(rb"Content-Length: (\d+)\r\n\r\n", data[at:])
        if not header:
            break
        length = int(header.group(1))
        start = at + header.end()
        out.append(json.loads(data[start:start + length]))
        at = start + length
    return out


def main():
    red, server = sys.argv[1], sys.argv[2]
    good = "fun helper(x) {\n  return x * 2;\n}\n\nprint(helper(21));\n"
    bad = "fun broken( {\n"

    session = b"".join(frame(m) for m in [
        {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}},
        {"jsonrpc": "2.0", "method": "textDocument/didOpen",
         "params": {"textDocument": {"uri": "file:///a.red", "text": good}}},
        {"jsonrpc": "2.0", "id": 2, "method": "textDocument/documentSymbol",
         "params": {"textDocument": {"uri": "file:///a.red"}}},
        {"jsonrpc": "2.0", "id": 3, "method": "textDocument/definition",
         "params": {"textDocument": {"uri": "file:///a.red"},
                    "position": {"line": 4, "character": 8}}},
        {"jsonrpc": "2.0", "method": "textDocument/didChange",
         "params": {"textDocument": {"uri": "file:///a.red"},
                    "contentChanges": [{"text": bad}]}},
        {"jsonrpc": "2.0", "id": 4, "method": "shutdown", "params": {}},
        {"jsonrpc": "2.0", "method": "exit", "params": {}},
    ])

    result = subprocess.run([red, server], input=session,
                            capture_output=True, timeout=120)
    if result.returncode != 0:
        print(result.stderr.decode(), file=sys.stderr)
        raise SystemExit(f"the server exited {result.returncode}")

    messages = parse(result.stdout)
    by_id = {m["id"]: m for m in messages if "id" in m}
    diagnostics = [m for m in messages
                   if m.get("method") == "textDocument/publishDiagnostics"]

    capabilities = by_id[1]["result"]["capabilities"]
    assert capabilities["documentSymbolProvider"], "no symbol provider"
    assert capabilities["definitionProvider"], "no definition provider"

    names = [s["name"] for s in by_id[2]["result"]]
    assert "helper" in names, f"helper not among {names}"

    where = by_id[3]["result"]
    assert where["range"]["start"]["line"] == 0, f"wrong definition: {where}"

    assert len(diagnostics) >= 2, "expected diagnostics for both versions"
    assert diagnostics[0]["params"]["diagnostics"] == [], \
        "the good file should be clean"
    assert diagnostics[-1]["params"]["diagnostics"], \
        "the broken file should not be"

    print(f"language server ok: {len(messages)} messages, "
          f"{len(names)} symbols, "
          f"{len(diagnostics[-1]['params']['diagnostics'])} diagnostics")


if __name__ == "__main__":
    main()
