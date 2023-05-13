Welcome to the source distribution of TreeSheets!
=================================================

This contains all the files needed to build TreeSheets for various platforms.
If instead you just want to USE TreeSheets, you may be better off with the binaries available on http://strlen.com/treesheets/

TreeSheets has been licensed under the ZLIB license (see ZLIB_LICENSE.txt).

![Workflow status](https://github.com/aardappel/treesheets/actions/workflows/build.yml/badge.svg)

`src` contains all source code. The code is dense, terse, and with few comments, typical for a codebase that was never
intended to be used by more than one person (me). On the positive side, you'll find the code very small and simple,
with all functionality easy to find and only in one place (no copy pasting or over-engineering). Enjoy.

`TS` is the folder that contains all user-facing files, typically the build process results in an executable to be put
in the root of this folder, and distributing to users is then a matter of giving them this folder.

`TODO.txt` is the random notes I kept on ideas of myself and others on what future features could be added.


Building:
---------
Note that YOU are responsible to know how to use compilers and C++, the hints below are all the help I will give you:

All Platforms:

- TreeSheets requires the latest wxWidgets from https://github.com/wxWidgets/wxWidgets.git

Windows:

- Make sure your `wxWidgets` folder sits parallel to the `src` folder, that way the TreeSheets project will pick
  it up without further modifications
- Inside `wxWidgets/build/msw`, open `wx_vc17.sln` with Visual Studio 2022.
- Select all projects (except the project `_custom_build`) in the solution explorer, and go to properties:
  - Set configuration to debug, and C/C++ -> Code Generation -> Runtime library
    to Multithreaded Debug
  - Set configuration to release, and C/C++ -> Code Generation -> Runtime library
    to Multithreaded
- Build solution in both x64 Debug and Release
- Close the wxWidgets solution
- "treesheets" contains the Visual Studio 2022 files for treesheets, open the .sln.
  If you've done the above correctly, TreeSheets will now compile and pick up
  the wxWidgets libraries.
- To distribute, build an installer with `TS_installer.nsi` (requires nsis.sourceforge.net)

Linux:

- Using the version of  wxWidgets from https://github.com/wxWidgets/wxWidgets.git
  - Follow the instructions to build there, but add `--enable-unicode` and
   `--disabled-shared` to the `configure` step.
- Build with `cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release` or similar.
  You can change the default installation prefix (`/usr/local`) by passing something like `-DCMAKE_INSTALL_PREFIX=/usr`.
- Install using `sudo make -C _build install`.
- There is also a `src/Makefile`, this is deprecated.

OSX:

- Build wxWidgets as follows (inside the wxWidgets dir):
  - `mkdir build_osx`
  - `cd build_osx`
  - `../configure --enable-unicode --disable-shared --disable-sys-libs --without-libtiff --with-osx_cocoa --enable-universal_binary=x86_64,arm64 CXXFLAGS="-stdlib=libc++" LDFLAGS="-stdlib=libc++" OBJCXXFLAGS="-stdlib=libc++" --disable-mediactrl CC=clang CXX=clang++`
  - `make -j8`
  - `sudo make install`
- use the XCode project in `osx/TreeSheets` to build treesheets. put the resulting
  .app together with the files from the `TS` folder in `osx/TreeSheetsBeta` to distribute.
  Note to use the "Archive" operation to create a release executable.

Contributing:
-------------
I welcome contributions, especially in the form of neatly prepared pull requests. The main thing to keep in mind when
contributing is to keep as close as you can to both the format and the spirit of the existing code, even if it goes
against the grain of how you program normally. That means not only using the same formatting and naming conventions
(which should be easy), but the same non-redundant style of code (no under-engineering, e.g. copy pasting,
and no over engineering, e.g. needless abstractions).

Also be economic in terms of features: treesheets tries to accomplish a lot with few features, additional user
interface elements (even menu items) have a cost, and features that are only useful for very few people should
probably not be in the master branch. Needless to say, performance is important too. When in doubt, ask me :)

Try to keep your pull requests small (don't bundle unrelated changes) and make sure you've done extensive testing
before you submit, preferrably on multiple platforms.

Stars over time:
----------------
[![Stargazers over time](https://starchart.cc/aardappel/treesheets.svg)](https://starchart.cc/aardappel/treesheets)