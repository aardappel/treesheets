This folder contains translations of the TreeSheets UI into various languages.

To work on these translations, you need xgettext/msginit/msgfmt commands,
which likely are already on your system on Linux/OSX, or on Windows you can
get them from e.g. https://mlocati.github.io/articles/gettext-iconv-windows.html

This generally follows the gettext standard, see e.g.
http://www.labri.fr/perso/fleury/posts/programming/a-quick-gettext-tutorial.html

To recompile the main template file (extracting strings from the source code),
run ../../src/genbot.bat or similar.

To create a translation for a new language, run (inside the tranlations directory):
msginit --input ts.pot --locale=de --ouput=de/ts.po
and replace "de" with the new language code.

To re-compile the language definitions, run de/compile.bat, or a copy of that file
in another language directory.

