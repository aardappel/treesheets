This folder contains translations of the TreeSheets UI into various languages.

To work on these translations, you need xgettext/msginit/msgfmt commands,
which likely are already on your system on Linux/OSX, or on Windows you can
get them from e.g. https://mlocati.github.io/articles/gettext-iconv-windows.html

This generally follows the gettext standard, see e.g.
http://www.labri.fr/perso/fleury/posts/programming/a-quick-gettext-tutorial.html

To recompile the main template file (extracting strings from the source code),
build the CMake target "update-pot":
  cmake --build <builddir> --target update-pot

To create a translation for a new language, run (inside TS/translations):
msginit --input ts.pot --locale=lang --output=lang/ts.po
replacing "lang" with either the 2-letter ISO 639-1 code (e.g. "it") or the
5-letter combination of the ISO 639 code + ISO 3166 country code (e.g. "pt_BR")
for the new language.

To merge the translation for an existing language with the strings from the
recompiled main template file (all languages at once), build the CMake
target "update-po":
  cmake --build <builddir> --target update-po

To re-compile the language definitions (all languages at once), build the
CMake target "update-mo":
  cmake --build <builddir> --target update-mo

The compiled translations (ts.mo) are embedded into the TreeSheets executable,
so rebuild TreeSheets to try out a changed translation.
