#!/bin/sh
xgettext -j --keyword=_ --sort-output --no-location -o ../TS/translations/ts.pot tsframe.h document.h system.h wxtools.h
