# AGENTS.md

Guidance for AI coding agents working on TreeSheets, a free-form hierarchical data organizer
(hierarchical spreadsheet) written in C++20 on top of wxWidgets. See `README.md` for the user-facing
overview; this file covers what you need to change the code safely.

## Repository layout

| Path | Contents |
| ---- | -------- |
| `src/` | All source code (≈14k lines, almost entirely headers) |
| `TS/` | User-facing data: `docs/`, `examples/*.cts`, `images/`, `scripts/*.lobster`, `translations/`, `readme*.html` |
| `cmake/` | CMake modules: `Lobster.cmake`, `WxPdfDoc.cmake`, `EmbedFiles.cmake`, `Localization.cmake`, `Packaging.cmake`, `UpdateScriptReference.cmake` |
| `platform/` | Per-OS files: Linux desktop/metainfo/MIME, `lsan.supp`, `toolchain-mingw64.cmake`; macOS `Info.plist`/icon, `toolchain-mingw64.cmake`; Windows `.rc`/icon |
| `.github/workflows/build.yml` | CI: Linux (x64, arm64; .deb and AppImage), Windows MSVC (x64, arm64), macOS (universal), then a release per release marker tag |
| `.claude/skills/treesheets-agent/` | Skill and wire protocol for driving a running TreeSheets over its agent socket |
| `.claude-plugin/` | `marketplace.json` and `plugin.json` that publish that skill as the Claude Code plugin `treesheets` |

### Unity build: one translation unit

`src/main.cpp` is the only TreeSheets `.cpp` file (plus `src/lobster_impl.cpp` for the Lobster
bindings, `src/stdafx.cpp` for the MSVC PCH, and `src/macclipboard.mm` on macOS). It defines the global
constants and the `A_*` action enum, then `#include`s every other header in dependency order
inside `namespace treesheets` (the Lobster implementation in `treesheets_impl.h` comes first):

```
image.h text.h cell.h grid.h selection.h encryption.h document.h evaluator.h system.h
wxtools.h tscanvas.h tsframe.h agent_server.h tsapp.h
```

As a result:
- New headers must be added to that include list at the right position. Headers have no
  include guards or includes of their own. They rely on what comes before them.
- All system/wx/std includes go in `src/stdafx.h` (the precompiled header), which also does
  `using namespace std;`.
- Everything is one namespace, so avoid short global names that can clash with Windows
  headers. For example, `TA_*`, `DT_*`, `SW_*` and `WM_*` are macros in `wingdi.h`/`winuser.h`.
  Such clashes build on Linux but break the Windows build.
- Some headers are included inside a struct scope. Don't add namespace-level `inline`
  variables there. Use `static inline` members or function-local statics.

### Core types

| File | Type | Role |
| ---- | ---- | ---- |
| `cell.h` | `Cell` | A node: `Text`, optional `Grid *grid`, colors, style, note, `parent` |
| `grid.h` | `Grid` | 2D array of `Cell`s, layout, rendering, most structural operations |
| `text.h` | `Text` | Cell text, editing, cursor handling, drawing (with `textruns.h` / `bidi.h` for RTL text) |
| `selection.h` | `Selection` | A rectangular selection in a grid, or a text cursor range in one cell |
| `encryption.h` | `Encryption` | Password protected files (Monocypher: Argon2id key, XChaCha20-Poly1305) |
| `document.h` | `Document` | One open file: load/save, undo/redo, rendering entry points, and `Action(int)`, the big dispatcher for every `A_*`/`wxID_*` command |
| `evaluator.h` | `Evaluator` | Cell operations/formulas (see `TS/examples/operation-reference.cts`) |
| `system.h` | `System` (`sys`) | Global state: settings (`sys->cfg`, wxConfig), file loading, fonts, images |
| `tsframe.h` | `TSFrame` | Main window: menus, toolbar, tabs, key and menu event routing |
| `tscanvas.h` | `TSCanvas` | Per-tab drawing surface (paint, mouse, keyboard) |
| `wxtools.h` | | wx helpers, `DrawText`, `TextLayoutCache` (direct Pango on wxGTK3), embedded file lookup |
| `tsapp.h` | `TSApp` | Startup and command-line parsing |
| `agent_server.h` | | Local token-authenticated socket for running Lobster scripts (`-a`) |
| `script_interface.h`, `treesheets_impl.h`, `lobster_impl.cpp` | | Lobster scripting API |

### Common change patterns

- **New command/menu item:** add an `A_*` value to the enum in `main.cpp`, add it with
  `MyAppend(menu, A_FOO, _("&Label"), _("Help text"))` in `tsframe.h` (toolbar button there too
  if needed), and handle it in `Document::Action` in `document.h`. Return a status string, or
  `nullptr`/empty on success, following the surrounding cases. Keyboard shortcuts are appended to the menu
  label (`_("&New") + "\tCTRL+N"`). Linux quirks: `Ctrl+Shift+U` is taken by GTK's Unicode input, and
  `CTRLORALT` means Alt on Linux/Windows and Ctrl on macOS.
- **Modifying the document:** call `AddUndo` on the affected cell/grid **before** changing it,
  as the existing actions do. Then trigger relayout/refresh the same way nearby code does
  (`canvas->Refresh()`, `ResetChildren()`, ...).
- **New setting:** a field on `System`, read in `System`'s constructor with `cfg->Read("name", ...)`,
  written with `sys->cfg->Write(...)` where it changes, and usually a check item in the Options menu.
- **New Lobster builtin:** add a pure virtual to `ScriptInterface` (`script_interface.h`),
  implement it in `treesheets_impl.h`, and register it in `lobster_impl.cpp` with the
  `BUILTIN(name, "args", "types", "returns", "doc")` macro (exposed as `ts.<name>`), with argument validation (`vm.BuiltinError(...)` on invalid input). Then
  regenerate `TS/docs/script_reference.html` with
  `cmake --build <builddir> --target update-script-reference` (this needs a display).
- **File format change:** bump `TS_VERSION` in `main.cpp`, write the new data in the relevant
  `Save()` (`cell.h`, `grid.h`, `text.h`, `document.h`), read it behind
  `if (sys->versionlastloaded >= N)` in the loaders so older files still open, and update
  `TS/docs/file_format_spec.txt`. Files from a newer version are rejected, so bump only when
  necessary.
- **Images, translations, the tutorial/operation reference `.cts` files and Lobster's standard
  modules** are compiled into the executable (`cmake/EmbedFiles.cmake` generates
  `embedded_{images,translations,examples,lobster}.h`); the `.cts` files are used when they are not
  on disk. Adding a PNG/SVG under `TS/images` or a
  `ts.mo` under `TS/translations` needs a CMake re-configure, but no code for the file lookup.

## Building

CMake ≥ 3.25 and a C++20 compiler are required. wxWidgets 3.3.2, Lobster and wxPdfDocument are
pinned (URL + SHA256) in `CMakeLists.txt`, `cmake/Lobster.cmake` and `cmake/WxPdfDoc.cmake` and are
fetched with `FetchContent`. Keep the `URL` lines in exactly that shape, because the Flathub
manifest scrapes these files to keep its dependency versions in sync.

```sh
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DTREESHEETS_BUNDLE_WXWIDGETS=ON
cmake --build _build -j            # binary only (add --config Release with MSVC)
cmake --build _build --target package -j   # .deb / .dmg / Inno Setup installer + zip
```

Options: `ENABLE_LOBSTER` (ON), `ENABLE_WXPDFDOC` (ON), `ENABLE_IPO` (ON; turn it OFF for faster
iteration), `ENABLE_ASAN` (OFF), `ENABLE_CLANG_TIDY` (OFF), `TREESHEETS_BUNDLE_WXWIDGETS` (OFF; ON
builds wx statically from source even when a system wx exists), and on Linux/BSD
`TREESHEETS_RELOCATABLE_INSTALLATION`. To skip downloads in extra build dirs, point
`FETCHCONTENT_SOURCE_DIR_WXWIDGETS` / `_LOBSTER` / `_WXPDFDOC` at an existing `_deps/*-src`.
On Linux, the build links GTK3/Pango directly when `gtk+-3.0` is found (`TREESHEETS_USE_PANGO`).

**Windows cross-check from Linux:** Windows CI uses MSVC, and it catches problems that
Linux GCC/Clang does not, such as Windows macro clashes and missing transitive includes (e.g.
`<span>`). Before pushing, build with MinGW as well (on macOS, use Homebrew's `mingw-w64` with
`platform/osx/toolchain-mingw64.cmake`):

```sh
cmake -S . -B _build_win32 -DCMAKE_TOOLCHAIN_FILE=platform/linux/toolchain-mingw64.cmake \
      -DCMAKE_BUILD_TYPE=Release -DENABLE_IPO=OFF
cmake --build _build_win32 --target TreeSheets -j$(nproc)
```

The resulting `.exe` runs under Wine. CI's Windows arm64 build uses `-DENABLE_LOBSTER=OFF`, so
code must compile without Lobster (`#ifdef ENABLE_LOBSTER`).

There is no unit-test suite. Verification means building on all relevant platforms and
exercising the running app.

## Running and testing

Command-line flags (`src/tsapp.h`): `-i` starts a separate instance (without it, the file is
passed to an already running TreeSheets), `-p` runs in portable mode (settings in
`TreeSheets.ini` in the working directory rather than the user config), `-a` opens the agent
socket, `-m` starts minimized. For scripted tests, use `-i -p` from a scratch directory, so you
don't touch the user's running instance, open tabs or settings.

- **Agent socket (`-a`):** run Lobster against the open document and read results back (see
  `TS/docs/AGENT_SOCKET.md`). Use the
  `treesheets-agent` skill, whose `SKILL.md` documents the newline-delimited JSON protocol. This is
  the preferred way to set up documents and check the model state (`ts.goto_selection()`,
  `ts.get_text()`, `ts.agent_result(...)`). Lobster's standard modules (`std`, `stdtype`, `vec`,
  `color`) are compiled into the executable, so a bare build directory runs scripts as is.
- **UI input (keys, mouse, dialogs):** never drive the user's live display. Use a separate
  X server/compositor (e.g. headless sway with Xwayland plus `xdotool`/`import -window`).
  Move the pointer in several small `xdotool mousemove` steps for drags.
- **Debug output:** TreeSheets writes stderr with `fputws`, which makes stderr wide-oriented, so
  plain `fprintf(stderr, ...)` output is silently lost. Use `fwprintf(stderr, L"...")`, or write
  to a file.
- **ASAN:** configure with `-DENABLE_ASAN=ON -DENABLE_IPO=OFF -DCMAKE_BUILD_TYPE=Debug`. Run with
  `LSAN_OPTIONS=suppressions=platform/linux/lsan.supp`, since the remaining reports are
  fontconfig/Pango process-lifetime caches.
- **Wine:** Arabic shaping/bidi and text metrics differ from real Windows, so don't judge RTL
  rendering there.

## Code style and contribution rules

From the maintainer (see "Contributing" in `README.md`):

- Match the existing code in format **and** spirit: dense, terse, few comments, no copy-paste,
  and no needless abstractions. The code puts each piece of functionality in one place, so keep it that way.
- Be economical with features. Every menu item or UI element has a cost. Performance matters.
- Keep changes small and focused, with no unrelated changes bundled in, and test on multiple platforms.
- Formatting: `.clang-format` (Google-based, 4-space indent, 100 columns, `Type *p`). Format only
  the lines you touch; don't reformat whole files.
- Naming: types in `PascalCase`, functions in `PascalCase`, fields/locals in lowercase without
  separators (`versionlastloaded`, `hovershade`), globals prefixed `g_`, actions `A_*`.
- Use `static_cast` over C casts and brace-initialize fields (`int x {0};`).
- All user-visible strings go through `_("...")` so they can be translated.

## Translations

gettext catalogs are in `TS/translations/<lang>/ts.po`, and the template is `TS/translations/ts.pot`.
After changing UI strings, run the targets `update-pot` → `update-po` → (translate) → `update-mo`
(see README "Translating"). The `update-pot` target extracts only from `tsframe.h`, `document.h`,
`system.h`, `wxtools.h`, `image.h` and `treesheets_impl.h`. If you add `_()` strings elsewhere,
extend `pot_sources` in `cmake/Localization.cmake`. The compiled `.mo` files are embedded into the
binary, so rebuild after `update-mo`.

## Git, CI and releases

- Upstream is `aardappel/treesheets`, whose default and CI branch is `master`. Every push to
  `master` builds all platforms, but only a release marker tag publishes a release:
  `git push upstream <commit>:refs/tags/release-bugfix` (or `release-minor`, `release-major`).
  This pushes the tag without creating it locally, so the marker can be reused.
- The `version` job in `build.yml` bumps the newest `vX.Y.Z` tag (2.3.6 becomes 2.3.7, 2.4.0 or
  3.0.0), and all builds use that version. The release is tagged `vX.Y.Z` on the marked commit
  (which must be on `master`) and becomes the latest release. Its notes list the commits since the
  last release of the same level: any `v*` for bugfix, `v*.*.0` for minor, `v*.0.0` for major.
  Then the marker is deleted. If the run fails, fix it and re-push the marker with `-f`.
- Builds that are not released use the last release version. Keep version arguments quoted in
  the Windows steps: PowerShell splits an unquoted `-DTREESHEETS_VERSION=2.0.0` at the first dot.
- The Linux job also makes an AppImage from the build of the `.deb`: it installs into an `AppDir`,
  links `docs`, `examples` and `scripts` next to the executable (TreeSheets looks there before the
  absolute install paths), and bundles the libraries with `linuxdeploy` and its GTK plugin. It is
  built on the oldest supported Ubuntu LTS (`ubuntu-22.04`, glibc 2.35, GCC 11), because the AppImage
  catalog (appimage.github.io) rejects AppImages that need a newer glibc. Move it on only when that
  release reaches its end of support. To test it without FUSE, run it with
  `APPIMAGE_EXTRACT_AND_RUN=1` and `-i -p`.
- Releases before v2.0.0 are tagged with the CI run number. Refer to those as `refs/tags/<n>`,
  since numeric tags can collide with branch names. The Debian epoch is 3 so that 2.x packages
  upgrade them.
- Commit messages: imperative, sentence-case subject that describes the user-visible effect
  ("Keep the cell under the pointer in place when hover zooming"), with a body that explains the why.
  "Fixes #N" in the message closes the issue on push to `master`.
- CI caches the whole `_build` directory per OS/arch/compiler, keyed on `CMakeLists.txt` and
  `cmake/**`. Changing those files causes one cold build (~10+ min on Windows/macOS). Keep the
  compiler id in any cache key you touch.
- Windows CI stays on MSVC. MinGW is only for local cross-checking.
- Don't commit build directories (`_build*/`, `build/`, `install_*`) or scratch files.
