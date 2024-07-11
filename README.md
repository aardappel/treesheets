<p align="center">
<img src="https://github.com/aardappel/treesheets/assets/3286756/8aaf7e24-70ee-4ed1-92ea-881b366ea570" />
</p>

TreeSheets is a "hierarchical spreadsheet" that is a great replacement for spreadsheets, mind mappers, outliners, PIMs, text editors and small databases.

Suitable for any kind of data organization, such as todo lists, calendars, project management, brainstorming, organizing ideas, planning, requirements gathering, presentation of information, etc.

It's like a spreadsheet, immediately familiar, but much more suitable for complex data because it's hierarchical.
It's like a mind mapper, but more organized and compact.
It's like an outliner, but in more than one dimension.
It's like a text editor, but with structure.

Community:
----------
If you like, you are kindly invited to join the [Discord channel](https://discord.gg/HAfKkJz) and 
the [Google group](https://groups.google.com/group/treesheets) for discussion.

Installation:
-------------

### Windows/Ubuntu LTS/MacOS users

Pre-built binaries are available at the
[Release section](https://github.com/aardappel/treesheets/releases). 

Please note that the Linux builds provided are built and only compatible with `ubuntu-latest` used by [GitHub Actions Runner](https://github.com/actions/runner-images). 

### Flatpak (Linux) users

If you use Flatpak, you can install [TreeSheets from Flathub](https://flathub.org/apps/com.strlen.TreeSheets).

Source Code:
------------
This repository contains all the files needed to build TreeSheets for various platforms.

### License

TreeSheets has been licensed under the ZLIB license (see ZLIB_LICENSE.txt).

![Workflow status](https://github.com/aardappel/treesheets/actions/workflows/build.yml/badge.svg)

### Structure

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

- TreeSheets requires the latest development wxWidgets from their repo:
  `git clone --recurse-submodules https://github.com/wxWidgets/wxWidgets.git`.

Windows:

1. Make sure your `wxWidgets` folder sits parallel to the `src` folder, that way the TreeSheets project will pick it up without further modifications
2. Inside `wxWidgets/build/msw`, open `wx_vc17.sln` with Visual Studio 2022.
3. Select all projects (except the project `_custom_build`) in the solution explorer, and go to properties:
    - Set configuration to debug, and C/C++ -> Code Generation -> Runtime library
      to Multithreaded Debug
    - Set configuration to release, and C/C++ -> Code Generation -> Runtime library
      to Multithreaded
4. Build solution in both x64 Debug and Release
5. Close the wxWidgets solution
6. "treesheets" contains the Visual Studio 2022 files for treesheets, open the .sln.
    If you've done the above correctly, TreeSheets will now compile and pick up
    the wxWidgets libraries.
7. To distribute, build an installer with `TS_installer.nsi` (requires nsis.sourceforge.net)

Linux:

1. Configure the build process with `cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release` or similar.
    - If you have `git` installed, the submodules for wxWidgets will be automatically updated and wxWidgets will be compiled as a CMake subproject. TreeSheets will be then statically linked against this wxWidgets build.
    - If you do like to link dynamically against an existing wxWidgets installation instead, you can switch off the option `GIT_WXWIDGETS_SUBMODULES` in the CMake project. In this case:
        - You can use the version of wxWidgets from https://github.com/wxWidgets/wxWidgets.git.
        - Follow the instructions to build there, but add `--enable-unicode` and `--disable-shared` to the `configure` step.
    - You can change the default installation prefix (`/usr/local`) by passing something like `-DCMAKE_INSTALL_PREFIX=/usr`.
2. Build using `cmake --build _build`.
3. Install using `sudo cmake --install _build`.

OSX:

1. Build wxWidgets as follows (inside the wxWidgets dir):
    1. `mkdir build_osx`
    2. `cd build_osx`
    3. `../configure --enable-unicode --disable-shared --disable-sys-libs --without-libtiff --with-osx_cocoa --enable-universal_binary=x86_64,arm64 CXXFLAGS="-stdlib=libc++" LDFLAGS="-stdlib=libc++" OBJCXXFLAGS="-stdlib=libc++" --disable-mediactrl CC=clang CXX=clang++`
    4. `make -j8`
    5. `sudo make install`
2. use the XCode project in `osx/TreeSheets` to build treesheets. put the resulting
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
