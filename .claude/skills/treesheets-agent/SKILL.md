---
name: treesheets-agent
description: >-
  Talk directly to a running TreeSheets instance over its local agent socket —
  run Lobster script (inline or from a file) against whatever document is
  currently open and get results or errors back. Works against any TreeSheets
  binary (dev build or installed distribution) on macOS, Linux or Windows
  (including a Windows build under Wine), source tree optional. Use
  when the user wants to inspect, automate, or script the currently open
  TreeSheets document, or asks to test/exercise the TreeSheets agent socket.
---

TreeSheets can run Lobster script against whatever document is open in a
running instance and hand a result back, over a local, token-authenticated
socket. It only exists when TreeSheets was launched with `-a` (agent mode) —
**and only in a binary built after this feature landed**; if `-a` produces
no endpoint, that TreeSheets predates it (see "No socket appears" below).

The transport depends on the platform; the protocol on top is the same:

| Platform | Endpoint | Token |
|---|---|---|
| macOS / Linux | Unix domain socket `/tmp/TreeSheets-agent-<user>.sock` | `<endpoint>.token` |
| Windows | TCP on `127.0.0.1`, port written to `%TEMP%\TreeSheets-agent-<user>.port` | `<endpoint>.token` |
| Windows build under Wine | same `.port` file, inside the Wine prefix (`<prefix>/drive_c/users/<user>/AppData/Local/Temp/`) | `<endpoint>.token` |

The Windows port is picked by the OS on every launch, so always read it from
the `.port` file. Only a Windows binary built after TCP support was added
writes one.

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
than assuming: the endpoint and token files from the table above exist only
while such an instance is up (substitute your actual username).

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
  - Windows: `where TreeSheets` in a shell, the Start-menu shortcut's target,
    or the usual `C:\Program Files\TreeSheets\TreeSheets.exe`, or ask. Launch
    it as `TreeSheets.exe -a`.
  - If you can't find an installed copy at all, say so and ask rather than
    silently giving up or fabricating a path.

### Windows build under Wine (Linux host)

A Windows `TreeSheets.exe` (e.g. from the MinGW cross-build in
`_build_win32/`) works under Wine, and the Linux-side client can reach it:
Wine maps the Windows TCP socket onto a real host socket on `127.0.0.1`.

- The exe needs its data next to it: `scripts/` (including Lobster's
  `modules/*.lobster`) and `images/`. A bare `cmake --build` directory
  doesn't have them; `cmake --install` into a folder, or copy `TS/scripts`,
  `TS/images` and the Lobster modules (`_build_win32/_deps/lobster-src/modules/{std,stdtype,vec,color}.lobster`
  into `scripts/modules/`) next to the exe.
- Launch it from that folder: `wine TreeSheets.exe -a -i`
  (add `WINEDEBUG=-all` to silence Wine's console noise).
- For testing, use a throwaway prefix instead of relying on `-i`, just like
  the throwaway `$HOME` advice below: `WINEPREFIX=/tmp/ts_wine wineboot -i`
  once (about 10 seconds), then `WINEPREFIX=/tmp/ts_wine wine TreeSheets.exe -a -i`.
  On Windows, TreeSheets keeps its settings (and the list of files to restore
  on startup) in the registry, which lives inside the prefix.
- **Older TreeSheets builds show an error box at startup in a new prefix**:
  *"Can't open registry key 'HKCU\Control Panel\International\User
  Profile' (error 2: File not found.)"*. It's harmless: a wxWidgets 3.3 bug
  logs an error when this key is missing, but the UI language is still
  detected correctly (fix proposed in wxWidgets/wxWidgets#27067). Current
  TreeSheets filters that message out (`SetupInternationalization()` in
  `src/tsapp.h`). For an older binary, the box stays open until someone
  clicks OK, which gets in the way of unattended runs, so create the key
  once per prefix before launching:
  `WINEPREFIX=/tmp/ts_wine wine reg add 'HKCU\Control Panel\International\User Profile' /f`.
- The client finds the `.port` file in `$WINEPREFIX` (or `~/.wine`) on its
  own when no native Linux socket exists, so export the same `WINEPREFIX` for
  it. Otherwise pass `--endpoint <prefix>/drive_c/users/<user>/AppData/Local/Temp/TreeSheets-agent-<user>.port`.
- `eval -f /host/path.lobster` works: the client converts the path to a
  Windows path that Wine can open (`winepath -w`, or `Z:\...` if `winepath`
  is missing). The error label then shows that Windows path.
- **Closing the window with unsaved changes shows an "are you sure?" prompt
  whose window title is empty**: `wmctrl -l` lists it as a window with a
  blank name (class `treesheets.exe`), not under the main window's title.
  Until someone answers it, the app doesn't exit and the `.port`/`.token`
  files stay. Answer it rather than killing the process; that leaves a stale
  autosave file behind, see below.

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
quirk — see `src/tsapp.h` if you have source). **Verified on Linux/GTK**: a
real window-close request (`wmctrl -c <window-id>`, or the window manager's
close button) goes through the ordinary close chain and removes both the
`.sock` and `.sock.token` files, no workaround needed. `SIGTERM`/`kill`
against the process, unsurprisingly, does *not* — that bypasses `OnClosing`
entirely, same as it would on any platform, so don't read a leftover
socket/token file after a forceful kill as a bug. Sending the `Ctrl+Q` /
`Exit` accelerator via a synthetic key event (`xdotool key ctrl+q`) was
unreliable in testing (silently did nothing, no dialog, no exit) — if you
need to script a quit for testing, prefer a real close request
(`wmctrl -c`) over synthesizing the keypress.

**A stale autosave file can make the app look hung on launch.** If a
`<name>.tmp` file exists next to `<name>.cts` when that file is (re)opened —
including via TreeSheets' own session restore on startup, not just an
explicit open — `LoadDB()` (`src/system.h` around line 230) pops a blocking
`wxMessageBox`: *"A temporary autosave file exists, would you like to load
it instead?"*. This is a genuinely separate top-level window (title
"Autosave load"), not a child of the main frame, so it's easy to miss in a
`wmctrl -l` grep for the main window's title. While it's open:
  - The tab/document being loaded doesn't finish loading, and normal window
    controls, plus the actual `Ctrl+Q`/close on that frame, **don't get
    ack'd until it's dismissed** — that's what "the app won't quit" usually
    is, not a socket-cleanup bug.
  - The agent socket itself stays responsive for whatever document *is*
    already loaded (`ping` and `eval` both keep working against the
    previously-active tab) — the modal only blocks the one file that's
    mid-load, not the whole process.
  - Dismiss it by clicking **No** (keep the saved file, discard the
    autosave) unless you specifically want the recovered content; after
    that, a normal close/quit works immediately.
  - This is caused by an *ungraceful* prior exit of TreeSheets on that same
    file (crash, `kill -9`, `SIGTERM`) leaving its periodic-autosave `.tmp`
    behind — so it's easy to trigger by accident while testing this skill
    itself (e.g. `pkill`ing a test instance instead of closing it), and then
    hitting it again on your *next* launch since session restore reopens
    the same files. Prefer closing test instances (`wmctrl -c`) over
    killing them to avoid seeding this for next time; if you do end up with
    a stale `.tmp` next to a real document, mention it rather than silently
    deleting — it lives next to the user's actual data.

### No socket (or `.port` file) appears even after a clean launch

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

It finds the endpoint and reads its per-launch token itself (see the table at
the top: the Unix socket, the Windows `.port` file, or a Wine prefix's `.port`
file if no native socket exists) — no setup needed beyond TreeSheets running
with `-a`. To target one explicitly, pass `--endpoint <socket or .port file>`
(`--socket` still works as an alias). On Windows, run it with `python` or
`py` instead of `python3` if that is how Python is installed. Pass `--json` (after the subcommand,
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
- **To test this skill itself (launching/killing instances, exercising quit
  behavior, etc.) without any risk to real documents**, launch with an
  isolated, throwaway `$HOME`/`$XDG_CONFIG_HOME` instead of relying on
  `-i` alone — `-i` only forces a *new process*, it does nothing to stop
  that process from reading the normal config and restoring the same real
  session tabs. E.g.:
  `HOME=/tmp/ts_test_home XDG_CONFIG_HOME=/tmp/ts_test_home/.config
  ./TreeSheets -a -i`. With no prior config there, it opens fresh (the
  bundled tutorial doc) instead of session-restoring the user's real files,
  and anything the test does — including intentionally crashing it to
  reproduce autosave-dialog behavior — stays inside that throwaway
  directory. Delete the throwaway `$HOME` afterward.
- `ts.save_document(saveas: int) -> int` writes the current document to disk
  — same as the Save (`saveas` false/0) / Save As (`saveas` true/1) menu
  actions, returns false on failure/cancel. Plain `save_document(false)` on a
  document that already has a filename (e.g. one just opened with
  `ts.load_document(...)`) saves silently with no dialog — that's the safe,
  scriptable case, and the one to use after editing a document you loaded or
  created via `new_document`. **If the document has no filename yet, or you
  pass `true`, it pops a blocking native "Save As" file dialog on the GUI
  thread** — `HandleLine()` runs `ScriptRun()` synchronously on the wx main
  thread with no timeout, so the `eval` call (and the whole app) hangs until
  a human fills in and confirms that dialog; this one is a TreeSheets-side
  behavior, not a Lobster engine bug, and is not affected by the Lobster
  version. Don't call it that way from an
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

## Resolved: recursive-traversal slowdown / nested-loop silent drop

Earlier versions of the bundled Lobster engine (pre-v2026.7) had two related
bugs, both filed as
[aardappel/lobster#449](https://github.com/aardappel/lobster/issues/449):
a recursive Lobster function combining `ts.*` native calls with `+=`
accumulation into a captured string went pathologically slow (tens of
seconds to minutes) past roughly 250-300 such calls, freezing the whole app
with no way to cancel; and, separately and much less reliably, a flat
non-recursive nested-`for`-loop script was once seen to silently drop its
response past ~700-800 native calls in one `eval`, though this could not be
reproduced on demand even on the buggy version.

Both are fixed upstream as of Lobster v2026.7, which `cmake/Lobster.cmake`
now pins. Re-verified directly: the same recursive-traversal repro that
used to reliably wedge the app at ~250-300 calls now completes in well
under 100ms at 1,600+ recursive native-call-laden invocations (5-6x the old
trigger scale), and the nested-loop repro returns correctly at ~800 calls.
Writing a straightforward recursive traversal (e.g. "dump the whole
document as text" as a recursive function) is safe again; no need to
rewrite it as an explicit-stack iterative loop.

If a single `eval` call still seems to hang or drop its response with no
error on this version, don't assume it's this — check the obvious things
first: shell-quoting mangling a `-c` inline script if you didn't write it
to a file, or a trailing `save_document(...)` call left over from a
copy-pasted script, which pops a **blocking native Save-As dialog** if the
document has no filename yet (see above) and looks identical to a full app
hang, `ping` included, from the outside.

## Protocol reference

Only needed if not using the script. Newline-delimited JSON over the Unix
domain socket (or, on Windows, a TCP connection to `127.0.0.1:<port from the
.port file>`), one request per line in, one response per line out:

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
