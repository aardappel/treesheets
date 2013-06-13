Welcome to the source distribution of TreeSheets!

This contains all the files needed to build TreeSheets for various platforms.
If instead you just want to USE TreeSheets, you may be better off with the binaries available on http://treesheets.com/

TreeSheets has been licensed under the ZLIB license (see ZLIB_LICENSE.txt).

src contains all source code. The code is dense, terse, and with few comments, typical for a codebase that was never intended
to be used by more than one person (me). On the positive side, you'll find the code very small and simple, with all functionality
easy to find and only in one place (no copy pasting or over-engineering). Enjoy.

TS is the folder that contains all user-facing files, typically the build process results in an executable to be put in the root
of this folder, and distributing to users is then a matter of giving them this folder. On windows, TS_installer.nsi
creates a nice installer (install nsis first).

TODO.txt is the random notes I kept on ideas of myself and others on what future features could be added.


Building:
=========
Note that YOU are responsible to know how to use compilers and C++, the hints below are all the help I will give you:

Windows:
- Download wxWidgets 2.9.4 and place it in the treesheets folder named "wxwidgets294" (parallel to "src").
- Inside wxwidgets294/build/msw, open wx_vc9.sln with Visual Studio 2010 (not 2008, the default)
- Select all projects in the solution explorer, and go to properties:
  Set configuration to debug,   and C/C++ -> Code Generation -> Runtime library to Multithreaded Debug
  Set configuration to release, and C/C++ -> Code Generation -> Runtime library to Multithreaded
- build solution in both debug and release
- close the wxWidgets sln
- "treesheets" contains the Visual Studio 2010 files for treesheets, open the .sln.
  If you've done the above correctly, TreeSheets will now compile and pick up the wxWidgets libraries.

Linux:
- build wxWidgets 2.9.4 as usual on linux, but use these arguments to configure:
  --enable-unicode --enable-optimize=-O2 --disable-shared
- in the src folder build.sh should now compile treesheets without errors.

OSX:
- build wxWidgets 2.9.4 as usual on OS X, but use whatever variant of these options to configure work for you:
  --enable-unicode --enable-optimize=-O2 --disable-shared --with-osx_cocoa
  CFLAGS="-arch i386" CXXFLAGS="-arch i386" CPPFLAGS="-arch i386" LDFLAGS="-arch i386" OBJCFLAGS="-arch i386" OBJCXXFLAGS="-arch i386"
  --with-macosx-sdk=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk
  --with-macosx-version-min=10.7
- use the xcode project in osx/TreeSheets to build treesheets. put the resulting .app together with the files from the TS folder in osx/TreeSheetsBeta to distribute.
