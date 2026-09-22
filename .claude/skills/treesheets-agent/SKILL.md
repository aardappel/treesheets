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
- `ts.save_document(saveas: int) -> int` writes the current document to disk
  — same as the Save (`saveas` false/0) / Save As (`saveas` true/1) menu
  actions, returns false on failure/cancel. Plain `save_document(false)` on a
  document that already has a filename (e.g. one just opened with
  `ts.load_document(...)`) saves silently with no dialog — that's the safe,
  scriptable case, and the one to use after editing a document you loaded or
  created via `new_document`. **If the document has no filename yet, or you
  pass `true`, it pops a blocking native "Save As" file dialog on the GUI
  thread** — same synchronous-`ScriptRun` situation as the recursive-traversal
  trap below: the `eval` call (and the whole app) hangs until a human
  fills in and confirms that dialog. Don't call it that way from an
  unattended script; if you need to save a brand-new, never-saved document
  non-interactively, give it a filename first some other way (e.g. save the
  `.cts` once by hand, or `ts.load_document()` an existing file before
  editing) so `save_document(false)` has a filename to write to. Or, better:
  use `ts.save_document_as(filename: string) -> int` instead, which writes to
  the given path directly and never shows a dialog — this is the one to use
  when scripting "create a new document and save it to a specific path"
  end-to-end without any human interaction. It also sets that path as the
  document's filename, so a later plain `save_document(false)` re-saves to
  the same place. Appends `.cts` automatically if the given filename has no
  extension.

## Known trap: recursive traversal + string concat hangs the whole app

Do **not** write a recursive Lobster function that both (a) calls `ts.*`
native functions (e.g. `goto_child`/`goto_parent`/`get_text`) and (b) mutates
a captured/outer string variable via `+=` on each call, e.g.:

```
def dump(depth):
    out += ts.get_text() + "\n"          # BAD: recursive + native calls + += to a captured var
    for(ts.num_children()) i:
        ts.goto_child(i)
        dump(depth + 1)
        ts.goto_parent()
```

This reliably goes pathologically slow (tens of seconds to minutes, for what
should be trivial work) once the traversal has made roughly **250-300 such
calls** — confirmed by isolating the variables experimentally: recursion
alone, `ts.*` calls alone, and `+=` accumulation alone are each fine even at
tens of thousands of iterations; only the combination of all three inside a
*recursive* function blows up. This looks like a refcounting/free-variable
bug in the bundled Lobster engine's TCC-JIT codegen path (`RunTCC` in
`src/lobster_impl.cpp`, `VM_JIT_MODE=1` — see `stdafx.h`), not a bug in
TreeSheets' own code, but it's real and easy to hit by accident (e.g. "dump
the whole document as text" is a very natural first script to write).

**Worse, there is no recovery once you hit it**: `agent_server.h`'s
`HandleLine()` runs `ScriptRun()` synchronously on the wx main/GUI thread with
no timeout or cancellation. A script that trips this doesn't just fail its
own request — it freezes the *entire app* (UI included) and the socket stops
accepting/answering *any* request (including `ping`) until the runaway script
eventually finishes on its own. There's nothing to do but wait it out (it
does eventually complete, it's pathologically slow, not truly infinite) — so
avoid triggering it rather than trying to cancel it once started.

**Fix: traverse iteratively, not recursively.** An explicit-stack `while`
loop calling the exact same `ts.*` functions and doing the exact same `+=`
accumulation has no such ceiling — verified traversing an entire ~60,000-cell
document (3.7MB of accumulated text) in ~10 seconds:

```
var ni = [0, 0, 0, 0, 0, 0, 0, 0]  // one slot per tree depth level you expect to reach
var depth = 0
var out = ""

ts.goto_root()
while true:
    out += ts.get_text() + "\n"
    let n = ts.num_children()
    if n > 0 and ni[depth] < n:
        let i = ni[depth]
        ni[depth] += 1
        ts.goto_child(i)
        depth += 1
        ni[depth] = 0
    else:
        if depth == 0: break
        ts.goto_parent()
        depth -= 1

ts.agent_result(out)
```

A **non-recursive** helper function called from a loop (i.e. the function
itself never calls itself) is also fine — the trap is specifically
recursion, not "having a function" or "calling `ts.*` from inside one". If
you need recursion for something else entirely (no `ts.*` calls, or no
mutation of a captured accumulator), that's fine too — it's only the
three-way combination that's dangerous. When in doubt, prefer the iterative
pattern above for any full-document traversal/dump.

### A second, harder-to-pin-down variant: nested `for` loops with no recursion, seen once

The above was diagnosed as specifically about *recursive* functions. On one
occasion it wasn't the whole story: a flat, non-recursive script — outer
`for(...)` over rows, inner `for(...)` over columns, calling `ts.*` natives
and accumulating into `out` with `+=`, exactly the "safe" shape described
above — silently dropped its response (no error, no crash) on a document
with ~700-800 total native calls for the request. Splitting the identical
work into two ~400-call `eval` calls returned correctly both times.

**This was not reproducible on demand.** Retried later on a freshly
relaunched process, deliberately matching the same nesting shape and pushing
to over **13,000** native calls / ~122KB of accumulated string in one
`eval` call — it succeeded every time, fast. So "~700-800 calls in one
`eval`" is not by itself a reliable trigger; whatever happened that one time
was not purely a function of a single script's call count. A plausible but
*unverified* guess: it depends on cumulative state built up over many prior
`eval` calls in the same long-lived process (each `eval` independently
JIT-compiles fresh machine code via `RunTCC`/libtcc — see the source
pointers below), not something a short-lived fresh process doing lots of
work in one call will hit. Filed as
[aardappel/lobster#449](https://github.com/aardappel/lobster/issues/449),
including this non-reproducibility as a correction to the original report.

**Practical takeaway:** if a single `eval` call seems to hang or drop its
response with no error, don't assume it's this — check the obvious things
first (shell-quoting mangling a `-c` inline script if you didn't write it to
a file; a trailing `save_document(...)` call left over from a copy-pasted
script, which pops a **blocking native Save-As dialog** if the document has
no filename yet — see above — and looks identical to a full app hang,
`ping` included, from the outside). Only reach for "split the work across
multiple `eval` calls" as a mitigation if a large script's response is
actually missing with the app otherwise idle and responsive to `ping`, not
as a reflexive precaution — the failure has not been shown to reproduce
reliably at any particular size on a fresh process.

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
