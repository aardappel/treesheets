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

TreeSheets must already be running, started with `-a`. Add `-i` too if you
want a fresh instance for testing instead of forwarding to one that's already
running. If nothing is running with `-a`, the socket and token files won't
exist and every request below fails to connect — say so plainly and offer to
launch it rather than guessing.

The socket/protocol code itself (`src/agent_server.h`) is platform-generic —
it's gated on `wxHAS_UNIX_DOMAIN_SOCKETS`, which wx defines for any Unix
(`__UNIX__ && !__WINDOWS__ && !__WINE__`), so macOS and Linux both get the
same `/tmp/TreeSheets-agent-<user>.sock` behavior. What differs between them
is only the binary layout and how a dev build finds its data files. The macOS
steps below were exercised directly in this repo's dev environment; the Linux
steps follow from reading `CMakeLists.txt` and `src/tsapp.h`'s `ResolvePath`/
`GetDataPath` but weren't run on an actual Linux box — sanity-check the first
launch before relying on it.

### macOS

```bash
./_build/TreeSheets.app/Contents/MacOS/TreeSheets -a
```

A dev build that was only `cmake --build`'d (never installed) is missing
`Contents/Resources` and hangs at startup behind an invisible modal alert
about missing icons — the agent socket never appears in that case. Fix once
with, from the build directory:

```bash
cmake --install . --prefix "$(pwd)"
```

### Linux

There's no app bundle — the build produces a plain `TreeSheets` binary
directly in the build directory:

```bash
./_build/TreeSheets -a
```

Resource lookup works differently here than on macOS. By default (unless
configured with `-DTREESHEETS_RELOCATABLE_INSTALLATION=ON`), `TREESHEETS_DATADIR`/
`TREESHEETS_DOCDIR` are baked into the binary at configure time as *absolute*
paths under `CMAKE_INSTALL_PREFIX` (GNUInstallDirs layout, e.g.
`<prefix>/share/TreeSheets`, `<prefix>/share/doc/TreeSheets`) — `ResolvePath()`
looks next to the executable first, then falls back to that compiled-in path.
So the macOS trick of `cmake --install . --prefix "$(pwd)"` from the build
directory won't line up on Linux; the same "hangs behind an invisible modal
about missing icons" failure is likely on an uninstalled dev build unless
either:

- you actually install to the configured prefix (`sudo cmake --install .`, or
  `sudo cmake --install . --prefix /usr/local` matching whatever
  `CMAKE_INSTALL_PREFIX` was at configure time), or
- you reconfigure with `-DTREESHEETS_RELOCATABLE_INSTALLATION=ON` first, which
  should make it behave like the macOS case (data resolved relative to the
  binary, so `cmake --install . --prefix "$(pwd)"` from the build dir works).

Quitting: the macOS-specific `wxEVT_END_SESSION` handling in `TSApp` (added to
work around Cmd+Q bypassing the normal close chain — see `src/tsapp.h`) is a
Cocoa quirk from how wx maps the Apple "quit" event. On Linux/GTK, closing the
window should go through the ordinary `wxEVT_CLOSE_WINDOW` →
`TSFrame::OnClosing()` → `wxApp::OnExit()` chain, so socket/token cleanup
should already be reliable there without it — but this hasn't been verified
on an actual GTK session either.

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

- **Look up the `ts.*` API in `TS/docs/script_reference.html` first** — it's
  the generated, authoritative function reference (signatures, param types,
  one-line docs) and needs no build: navigation (`goto_root`, `goto_child`,
  `goto_parent`, `goto_selection`, `goto_column_row`...), reading/writing
  cells (`get_text`, `set_text`, `get_note`, `set_note`...), grid ops
  (`create_grid`, `insert_column`, `insert_row`, `delete`...), document
  creation (`new_document`), styling, images, and more. It's also installed
  into any built app at `Contents/Resources/docs/script_reference.html`
  (`TreeSheets.app/Contents/Resources/...` on macOS) and reachable in the UI
  via Help > Script reference. If it looks stale for a given build, or this
  file isn't available at all, regenerate it straight from that binary with
  `TreeSheets -d` (writes `builtin_functions_reference.html`, covering every
  builtin including non-`ts` ones, into the current directory) — only fall
  back to reading `src/script_interface.h` / `src/lobster_impl.cpp` if
  neither is available.
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
