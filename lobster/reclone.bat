rem First delete the existing clone to leave no unused files.

rmdir /s /q src
rmdir /s /q lobster
rmdir /s /q include
rmdir /s /q external
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
md external
md external\flatbuffers
md external\flatbuffers\src
xcopy %source%\dev\external\flatbuffers\src\*.* external\flatbuffers\src
md include\StackWalker
xcopy %source%\dev\include\StackWalker\*.* include\StackWalker
md include\gsl
xcopy %source%\dev\include\gsl\*.* include\gsl
md ..\TS\scripts\modules
xcopy %source%\modules\stdtype.lobster ..\TS\scripts\modules
xcopy %source%\modules\std.lobster ..\TS\scripts\modules
xcopy %source%\modules\vec.lobster ..\TS\scripts\modules
xcopy %source%\modules\color.lobster ..\TS\scripts\modules
