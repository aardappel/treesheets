Welcome to the source distribution of TreeSheets!

This contains all the files needed to build TreeSheets for various platforms.
If instead you just want to USE TreeSheets, you may be better off with the binaries available on http://treesheets.com/

TreeSheets has been licensed under the ZLIB license (see ZLIB_LICENSE.txt).

src contains all source code. The code is dense, terse, and with few comments, typical for a codebase that was never intended to be used by more than one person (me). On the positive side, you'll find the code very small and simple, with all functionality easy to find and only in one place (no copy pasting or over-engineering). Enjoy.

TS is the folder that contains all user-facing files, typically the build process results in an executable to be put in the root of this folder, and distributing to users is then a matter of giving them this folder.

TODO.txt is the random notes I kept on ideas of myself and others on what future features could be added.


Building:
=========
Note that YOU are responsible to know how to use compilers and C++, the hints below are all the help I will give you:

All Platforms:
- TreeSheets requires wxWidgets 3.0.0 or better.
  Bleeding edge from https://github.com/wxWidgets/wxWidgets.git may be better than the last public release.

Windows:
- Make sure your wxWidgets folder sits parallel to the src folder, that way the TreeSheets project will pick
  it up without further modifications
- (if from git): copy include\wx\msw\setup0.h to include\wx\msw\setup.h
- Inside wxWidgets/build/msw, open wx_vc10.sln with Visual Studio 2010
- Select all projects in the solution explorer, and go to properties:
  Set configuration to debug,   and C/C++ -> Code Generation -> Runtime library
  to Multithreaded Debug
  Set configuration to release, and C/C++ -> Code Generation -> Runtime library
  to Multithreaded
- build solution in both debug and release
  (if fails, may have to build again)
- close the wxWidgets sln
- "treesheets" contains the Visual Studio 2010 files for treesheets, open the .sln.
  If you've done the above correctly, TreeSheets will now compile and pick up
  the wxWidgets libraries.
- to distribute, build an installer with TS_installer.nsi (requires nsis.sourceforge.net)

Linux:
- build wxWidgets 3.0.0 (or better) as usual on linux, but use these arguments to configure:
  --enable-unicode --enable-optimize=-O2 --disable-shared
- in the src folder build.sh should now compile treesheets without errors.
- the exe is in the TS folder, tgz this folder to distribute

OSX:
- build wxWidgets 3.0.0 (or better) as usual on OS X, but use whatever variant of these options 
  to configure work for you:
  --enable-unicode --enable-optimize=-O2 --disable-shared --with-osx_cocoa
  CFLAGS="-arch i386" CXXFLAGS="-arch i386" CPPFLAGS="-arch i386" LDFLAGS="-arch i386"
  OBJCFLAGS="-arch i386" OBJCXXFLAGS="-arch i386"
  --with-macosx-sdk=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.7.sdk
  --with-macosx-version-min=10.7
- alternatively, replace 10.7 in the above 2 locations with 10.6, and google how 
  to install the old 10.6 SDK in the newest Xcode (which only comes with 10.7 onwards).
- use the xcode project in osx/TreeSheets to build treesheets. put the resulting
  .app together with the files from the TS folder in osx/TreeSheetsBeta to distribute.


Contributing:
=============
I welcome contributions, especially in the form of neatly prepared pull requests. The main thing to keep in mind when contributing is to keep as close as you can to both the format and the spirit of the existing code, even if it goes against the grain of how you program normally. That means not only using the same formatting and naming conventions (which should be easy), but the same non-redundant style of code (no under-engineering, e.g. copy pasting, and no over engineering, e.g. needless abstractions).

Also be economic in terms of features: treesheets tries to accomplish a lot with few features, additional user interface elements (even menu items) have a cost, and features that are only useful for very few people should probably not be in the master branch. Needless to say, performance is important too. When in doubt, ask me :)

Try to keep your pull requests small (don't bundle unrelated changes) and make sure you've done extensive testing before you submit.
