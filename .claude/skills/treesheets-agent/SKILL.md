---
name: treesheets-agent
description: >-
  Talk directly to a running TreeSheets instance over its local agent socket —
  run Lobster script (inline or from a file) against whatever document is
  currently open and get results or errors back. Use when the user wants to
  inspect, automate, or script the currently open TreeSheets document, or asks
  to test/exercise the TreeSheets agent socket.
---

TreeSheets (this repo) can run Lobster script against whatever document is
open in a running instance and hand a result back, over a local,
token-authenticated Unix domain socket. It only exists when TreeSheets was
launched with `-a` (agent mode). Implementation: `src/agent_server.h`,
`src/tsapp.h`, `src/treesheets_impl.h`, `src/lobster_impl.cpp`. Currently
macOS/Linux only (Unix domain sockets); there is no Windows equivalent yet.

The harness reports the absolute path to this `SKILL.md`. Resolve
`scripts/ts_agent.py` relative to its parent directory, even when the current
working directory is elsewhere.

## Prerequisites

TreeSheets must already be running, started with `-a`. From a dev build in
this repo:

```bash
./_build/TreeSheets.app/Contents/MacOS/TreeSheets -a
```

Add `-i` too if you want a fresh instance for testing instead of forwarding to
one that's already running. If nothing is running with `-a`, the socket and
token files won't exist and every request below fails to connect — say so
plainly and offer to launch it rather than guessing.

A dev build that was only `cmake --build`'d (never installed) is missing
`Contents/Resources` and hangs at startup behind an invisible modal alert
about missing icons — the agent socket never appears in that case. Fix once
with, from the build directory:

```bash
cmake --install . --prefix "$(pwd)"
```

## Talking to it

Use the bundled client:

```bash
python3 /absolute/path/to/treesheets-agent/scripts/ts_agent.py ping
python3 /absolute/path/to/treesheets-agent/scripts/ts_agent.py eval -c "ts.agent_result(string(1 + 1))"
python3 /absolute/path/to/treesheets-agent/scripts/ts_agent.py eval -f /path/to/script.lobster
```

It finds the socket and reads its per-launch token itself
(`/tmp/TreeSheets-agent-<user>.sock` and `.sock.token`) — no setup needed
beyond TreeSheets running with `-a`. Pass `--json` (after the subcommand,
e.g. `eval -c "..." --json`) to get the raw `{"ok":...,"error":...,"result":...}`
response instead of the human-readable summary; prefer `--json` when parsing
output programmatically. Exit code is 0
on `ok`, 1 otherwise.

## Writing the Lobster side

- The full `ts.*` API lives in `src/script_interface.h` /
  `src/lobster_impl.cpp`: navigation (`goto_root`, `goto_child`,
  `goto_parent`, `goto_selection`, `goto_column_row`...), reading/writing
  cells (`get_text`, `set_text`, `get_note`, `set_note`...), grid ops
  (`create_grid`, `insert_column`, `insert_row`, `delete`...), styling,
  images, and more.
- `ts.agent_result(s)` is the one addition made for this channel: call it to
  hand a string back in the response's `result` field. Without it, `result`
  is always `""`.
- Every `eval` starts back at the document root (`ScriptRun` resets position
  each call). Most mutators (`set_text`, etc.) are no-ops on the root cell
  since it has no parent — `goto_child(n)`/`goto_selection()` first.
- Compile errors come back in `error` in Lobster's usual
  `<label>(<line>): error: ...` format. A bare `eval -c` uses the label
  `"agent"`; `eval -f` uses the real file path.
- `int2` values (from `num_columns_rows()`, or arguments to `delete(...)`)
  print as `int2{x, y}` and are indexed as `v[0]`/`v[1]`; construct one as
  `int2{x, y}`. `create_grid(cols, rows)` only creates a grid if the current
  cell doesn't have one yet — safe to call speculatively.
- **The currently open document is very likely real user data** (TreeSheets
  restores the last session's open files on launch, `-a`/`-i` don't change
  that) — check `ts.get_filename()` first. To experiment with a grid without
  touching existing content: `insert_column(xs)` (or `insert_row(ys)`) at the
  end of the root grid, `goto_column_row(xs, 0)` into the new cell, do the
  work there, then `goto_root(); delete(int2{xs, 0}, int2{1, ys})` — deleting
  a fully-emptied column/row also removes it, so this restores the original
  shape exactly.

## Protocol reference

Only needed if not using the script. Newline-delimited JSON over the Unix
domain socket, one request per line in, one response per line out:

```
-> {"id":"1","token":"<token>","cmd":"ping"}
<- {"id":"1","ok":true,"error":"","result":"pong"}

-> {"id":"2","token":"<token>","cmd":"eval","code":"ts.agent_result(string(1+1))"}
<- {"id":"2","ok":true,"error":"","result":"2"}

-> {"id":"3","token":"<token>","cmd":"eval","file":"/path/to/script.lobster"}
<- {"id":"3","ok":true,"error":"","result":""}
```

`code` takes precedence over `file` if both are given. A wrong `token` gets
`{"ok":false,"error":"bad token",...}`; no document open gets
`{"ok":false,"error":"no document open",...}`.
