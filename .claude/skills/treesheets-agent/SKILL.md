---
name: treesheets-agent
description: >-
  Talk directly to a running TreeSheets instance over its local agent socket —
  run Lobster script (inline or from a file) against whatever document is
  currently open and get results or errors back. Works against any TreeSheets
  binary (dev build or installed distribution), source tree optional. Use
  when the user wants to inspect, automate, or script the currently open
  TreeSheets document, or asks to test/exercise the TreeSheets agent socket.
---

TreeSheets can run Lobster script against whatever document is open in a
running instance and hand a result back, over a local, token-authenticated
Unix domain socket. It only exists when TreeSheets was launched with `-a`
(agent mode) — **and only in a binary built after this feature landed**; if
`-a` produces no socket, that TreeSheets predates it (see "No socket appears"
below). Currently macOS/Linux only (Unix domain sockets); no Windows
equivalent yet.

Nothing in this skill requires the TreeSheets source tree — the socket, the
protocol, and `scripts/ts_agent.py` all work against a bare installed binary.
Source references below (file paths under `src/`) are call-outs for when you
happen to have the source checked out, not a requirement; skip them freely.

The harness reports the absolute path to this `SKILL.md`. Resolve
`scripts/ts_agent.py` relative to its parent directory — that's this skill's
own directory, independent of wherever the TreeSheets binary you're talking
to lives.

## Prerequisites

TreeSheets must already be running, started with `-a`. Check first rather
than assuming: the socket and token exist at `/tmp/TreeSheets-agent-<user>.sock`
and `.sock.token` (substitute your actual username) only while such an
instance is up.

If nothing is running with `-a`, launch (or relaunch) one — add `-i` too if
you want a fresh instance for testing instead of forwarding to one that's
already running:

- **If you have a TreeSheets source checkout and dev build** (e.g. this
  repo): `./_build/TreeSheets.app/Contents/MacOS/TreeSheets -a` on macOS, or
  `./_build/TreeSheets -a` on Linux (see "Dev-build gotcha" below).
- **If you only have an installed/binary distribution**, find it first — do
  not guess a path:
  - macOS: `mdfind "kMDItemCFBundleIdentifier == 'com.strlen.TreeSheets'"` or
    `mdfind -name TreeSheets.app`, or just ask the person where it's
    installed. Then `open -a "<path to TreeSheets.app>" --args -a` (or run
    the binary inside `Contents/MacOS/` directly, same as the dev-build
    case, if you need to see its stdout/stderr).
  - Linux: `command -v treesheets` / `command -v TreeSheets`, or check
    whatever package manager installed it (`dpkg -L`/`rpm -ql` for the
    TreeSheets package), or ask.
  - If you can't find an installed copy at all, say so and ask rather than
    silently giving up or fabricating a path.

**Dev-build gotcha (only affects a `cmake --build`-only checkout, not an
installed distribution):** the resources directory may be missing, which
makes the app hang at startup behind an invisible modal alert about missing
icons — the socket never appears in that case. On macOS, fix once from the
build directory with `cmake --install . --prefix "$(pwd)"`. On Linux this
doesn't translate directly (see "Linux" below); a proper installed
distribution already has this handled by its installer/package, so this
whole gotcha doesn't apply there.

### Linux resource-path notes (dev builds only)

By default (unless configured with `-DTREESHEETS_RELOCATABLE_INSTALLATION=ON`),
`TREESHEETS_DATADIR`/`TREESHEETS_DOCDIR` are baked into the binary at
configure time as *absolute* paths under `CMAKE_INSTALL_PREFIX` (GNUInstallDirs
layout, e.g. `<prefix>/share/TreeSheets`, `<prefix>/share/doc/TreeSheets`);
`ResolvePath()` looks next to the executable first, then falls back to that
compiled-in path. So the macOS `cmake --install . --prefix "$(pwd)"` dev
trick doesn't line up on Linux — either actually install to the configured
prefix (`sudo cmake --install .`), or reconfigure with
`-DTREESHEETS_RELOCATABLE_INSTALLATION=ON` first so it behaves like the
macOS case. (Sourced from reading `CMakeLists.txt`/`src/tsapp.h`, not
verified on an actual Linux machine.)

Cleanup on quit: macOS needed an explicit `wxEVT_END_SESSION` handler because
Cmd+Q / AppleScript "quit" bypass the normal close chain there (a Cocoa/wx
quirk — see `src/tsapp.h` if you have source). On Linux/GTK, closing the
window should go through the ordinary close chain and clean up the
socket/token files without needing that workaround — but this hasn't been
verified on an actual GTK session. If you find a stale socket/token file
after quitting on Linux, that's the first thing to check.

### No socket appears even after a clean launch

Two different causes, worth telling apart:

1. **Missing resources** (dev build only, see above) — the process hangs
   before ever reaching the point where it opens the socket.
2. **This binary predates the agent-socket feature** — it starts fine, `-a`
   is silently ignored (unrecognized single-char flags are no-ops), and no
   socket ever appears. There's nothing to talk to until that TreeSheets is
   rebuilt from a source tree that has this feature, or a newer release
   ships it. Don't spend long debugging a launch that "works" but never
   produces a socket — check this early.

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
output programmatically. Exit code is 0 on `ok`, 1 otherwise.

## Writing the Lobster side

Look up the `ts.*` API — navigation (`goto_root`, `goto_child`,
`goto_parent`, `goto_selection`, `goto_column_row`...), reading/writing
cells (`get_text`, `set_text`, `get_note`, `set_note`...), grid ops
(`create_grid`, `insert_column`, `insert_row`, `delete`...), document
creation (`new_document`), styling, images, and more — from whichever of
these is available, in this order:

1. **The running app's own Help > Script reference menu item** — works no
   matter how TreeSheets was installed.
2. **`TS/docs/script_reference.html`**, if you have the source tree — the
   generated, authoritative function reference, no build needed to read it.
3. **The installed copy of that same file** next to wherever TreeSheets put
   its docs (e.g. `Contents/Resources/docs/script_reference.html` inside a
   macOS `.app`; location varies for Linux packages) — search for
   `script_reference.html` under the app's install location if unsure.
4. **Regenerate it straight from the binary**, no source or existing docs
   install required: `<treesheets-binary> -d`. This writes
   `builtin_functions_reference.html` (covering every builtin, not just
   `ts.*`) into a directory derived from that build's own data path, not
   necessarily the current directory or the binary's directory — after
   running it, search for the file (e.g. `find <install-or-build-dir> -name
   builtin_functions_reference.html`) rather than assuming where it landed.
5. Only if you have the source tree and everything above is somehow
   unavailable: read `src/script_interface.h` / `src/lobster_impl.cpp`
   directly.

Other things worth knowing regardless of how you looked up the API:

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
  shape exactly. Or use `ts.new_document(cols, rows)` to open a fresh,
  unsaved tab instead of touching whatever's already open.

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
