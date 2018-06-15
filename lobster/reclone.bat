rem First delete the existing clone to leave no unused files.

rmdir /s /q src
rmdir /s /q lobster
rmdir /s /q include
rmdir /s /q ..\TS\scripts\include

rem Copy selected dirs we need to build just the language core.

rem This currently assumes the lobster repo is parallel to the treesheets one.
rem TODO: make this configurable and/or allow it to do a git clone.
set source=..\..\lobster

md src
xcopy %source%\dev\src\*.cpp src
md src\lobster
xcopy %source%\dev\src\lobster\*.h src\lobster
md lobster
xcopy %source%\dev\lobster\language.vcxproj lobster
xcopy %source%\dev\lobster\language.vcxproj.filters lobster
md include
md include\flatbuffers
xcopy %source%\dev\include\flatbuffers\*.* include\flatbuffers
md include\StackWalker
xcopy %source%\dev\include\StackWalker\*.* include\StackWalker
md include\gsl
xcopy %source%\dev\include\gsl\*.* include\gsl
md ..\TS\scripts\include
xcopy %source%\lobster\include\stdtype.lobster ..\TS\scripts\include
xcopy %source%\lobster\include\std.lobster ..\TS\scripts\include
xcopy %source%\lobster\include\vec.lobster ..\TS\scripts\include
xcopy %source%\lobster\include\color.lobster ..\TS\scripts\include

