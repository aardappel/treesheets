# Translation maintenance targets and installation of the compiled translations.

file(GLOB po_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/TS/translations/*/ts.po")

# Target to regenerate the translation template from the strings in the source code:
#   cmake --build <builddir> --target update-pot
find_program(XGETTEXT_EXECUTABLE xgettext)
if(XGETTEXT_EXECUTABLE)
    set(pot_sources tsframe.h document.h system.h wxtools.h)
    add_custom_target(
        update-pot
        COMMAND
            "${XGETTEXT_EXECUTABLE}" --from-code=UTF-8 --keyword=_ --sort-output --no-location
            -o "${CMAKE_CURRENT_SOURCE_DIR}/TS/translations/ts.pot" ${pot_sources}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
        COMMENT "Updating TS/translations/ts.pot from the source code"
        VERBATIM
    )
endif()

# Target to merge the strings of the translation template (.pot) into the translations (.po):
#   cmake --build <builddir> --target update-po
find_program(MSGMERGE_EXECUTABLE msgmerge)
if(MSGMERGE_EXECUTABLE)
    set(po_commands)
    foreach(po_file IN LISTS po_files)
        list(APPEND po_commands COMMAND "${MSGMERGE_EXECUTABLE}" --update --backup=none
             "${po_file}" "${CMAKE_CURRENT_SOURCE_DIR}/TS/translations/ts.pot")
    endforeach()
    add_custom_target(
        update-po
        ${po_commands}
        COMMENT "Merging TS/translations/ts.pot into TS/translations/*/ts.po"
        VERBATIM
    )
endif()

# Target to recompile the translations (.po) into the binary form used by the program (.mo):
#   cmake --build <builddir> --target update-mo
find_program(MSGFMT_EXECUTABLE msgfmt)
if(MSGFMT_EXECUTABLE)
    set(mo_commands)
    foreach(po_file IN LISTS po_files)
        cmake_path(REPLACE_EXTENSION po_file LAST_ONLY ".mo" OUTPUT_VARIABLE mo_file)
        list(APPEND mo_commands COMMAND "${MSGFMT_EXECUTABLE}" -o "${mo_file}" "${po_file}")
    endforeach()
    add_custom_target(
        update-mo
        ${mo_commands}
        COMMENT "Compiling TS/translations/*/ts.po into ts.mo files"
        VERBATIM
    )
endif()

# Install translations to correct platform-specific path.
# See: https://docs.wxwidgets.org/trunk/overview_i18n.html#overview_i18n_mofiles
file(GLOB mo_files "${CMAKE_CURRENT_SOURCE_DIR}/TS/translations/*/ts.mo")
foreach(mo_file IN LISTS mo_files)
    cmake_path(GET mo_file PARENT_PATH locale_dir)
    cmake_path(GET locale_dir FILENAME locale)

    if(WIN32 OR TREESHEETS_RELOCATABLE_INSTALLATION)
        # Paths must be relative to use with CPack
        install(FILES "${mo_file}" DESTINATION "translations/${locale}")
    elseif(APPLE)
        # Paths must be relative to use with CPack
        install(FILES "${mo_file}" DESTINATION "TreeSheets.app/Contents/Resources/translations/${locale}.lproj")
    else()
        # Falling back to GNU scheme
        install(FILES "${mo_file}" DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${locale}/LC_MESSAGES")
    endif()
endforeach()
