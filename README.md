<p align="center">
  <img src="https://github.com/user-attachments/assets/1d6dc57a-5db2-48ce-82b9-5e7675bf0e7d">
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

Please note that the packages for Debian-based distributions provided are built on `ubuntu-latest` used by [GitHub Actions Runner](https://github.com/actions/runner-images). They could also be installed on other Debian-based distributions depending on whether the required dependency packages are available.

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

### Windows

1. Clone this repository
```sh
git clone https://github.com/aardappel/treesheets
```
2. Import into Visual Studio as CMake Project.

See
[Visual Studio online documentation](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170) for more details with regards to configure, build and install.

Alternatively you can run the Visual Studio Developer Command Prompt and in the TreeSheets source directory execute

```sh
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release  -DGIT_WXWIDGETS_SUBMODULES=ON
cmake --build _build --config Release --target package -j
```
to configure the Visual Studio build system, build TreeSheets and package it.

### Mac OS
  
1. Clone this repository
```sh
git clone https://github.com/aardappel/treesheets
```
2. Change to working tree
```sh
cd treesheets
```
3. Configure the build system
```sh
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/Applications
```

Please note that you need to have wxWidgets installed, e.g. distributed by Homebrew or built by yourself. 
If you wish to compile statically against wxWidgets, append `-DGIT_WXWIDGETS_SUBMODULES=ON` (autodownloads wxWidgets) or `-DTREESHEETS_WITH_STATIC_WXWIDGETS=ON` (if you have already placed the wxWidgets source in `lib/wxWidgets`).

4. Build
```sh
cmake --build _build -j
```
5. Install
```sh
cmake --install _build
```

### Linux
  
1. Clone this repository
```sh
git clone https://github.com/aardappel/treesheets
```
2. Change to working tree
```sh
cd treesheets
```
3. Configure the build system
```sh
cmake -S . -B _build -DCMAKE_BUILD_TYPE=Release
```

Please note that you need to have wxWidgets installed, e.g. distributed by your distribution or built by yourself. 
If you wish to compile statically against wxWidgets, append `-DGIT_WXWIDGETS_SUBMODULES=ON` (autodownloads wxWidgets) or `-DTREESHEETS_WITH_STATIC_WXWIDGETS=ON` (if you have already placed the wxWidgets source in `lib/wxWidgets`).

4. Build
```sh
cmake --build _build -j
```
5. Install
```sh
sudo cmake --install _build
```

### Further information for Mac OS / Linux
<details>

 - If you like to build wxWidgets by yourself:
    - You can use the version of wxWidgets from https://github.com/wxWidgets/wxWidgets.git.
    - Follow the instructions to build there, but add `--enable-unicode` and `--disable-shared` to the `configure` step.
- You can change the default installation prefix (`/usr/local`) by passing something like `-DCMAKE_INSTALL_PREFIX=/usr`.
- If you are MacOS X user, a bundle will be installed to the installation prefix.

</details>

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
[![Stargazers over time](https://starchart.cc/aardappel/treesheets.svg?variant=adaptive)](https://starchart.cc/aardappel/treesheets)
