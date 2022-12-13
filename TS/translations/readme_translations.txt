This folder contains translations of the TreeSheets UI into various languages.

To work on these translations, you need xgettext/msginit/msgfmt commands,
which likely are already on your system on Linux/OSX, or on Windows you can
get them from e.g. https://mlocati.github.io/articles/gettext-iconv-windows.html

This generally follows the gettext standard, see e.g.
http://www.labri.fr/perso/fleury/posts/programming/a-quick-gettext-tutorial.html

To recompile the main template file (extracting strings from the source code),
run src/genpot.bat or similar.

To create a translation for a new language, run (inside TS/translations):
msginit --input ts.pot --locale=lang --ouput=lang/ts.po
replacing "lang" with either the 2-letter ISO 639-1 code (e.g. "it") or the
5-letter combination of the ISO 639 code + ISO 3166 country code (e.g. "pt_BR"),
for the new language.

To merge the translation for an existing language with the strings from the
recompiled main template file, run lang/merge.bat, replacing "lang" as
described above.

To re-compile the language definitions, run lang/compile.bat, replacing "lang"
as described above.
