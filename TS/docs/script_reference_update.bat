rem NOTE: this uses the Release mode executable!
..\TreeSheets.exe -d
del script_reference.html
move ..\scripts\builtin_functions_reference.html script_reference.html
pause
