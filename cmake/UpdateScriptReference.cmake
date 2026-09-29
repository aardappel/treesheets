# Runs `TreeSheets -d` to dump the builtin function documentation and stores it as OUTPUT.
# Usage: cmake -DTREESHEETS_EXE=<exe> -DSCRIPTS_SRC=<TS/scripts> -DOUTPUT=<html> -P <this file>

cmake_path(GET TREESHEETS_EXE PARENT_PATH exe_dir)
if(APPLE)
    # Inside a .app bundle, GetDataPath()/ResolvePath() (src/tsapp.h) resolve the "scripts/"
    # data dir to Contents/Resources, not next to the executable in Contents/MacOS.
    cmake_path(GET exe_dir PARENT_PATH bundle_contents_dir)
    set(scripts_dir "${bundle_contents_dir}/Resources/scripts")
else()
    set(scripts_dir "${exe_dir}/scripts")
endif()
set(dump "${scripts_dir}/builtin_functions_reference.html")

# The dump is written into the resolved scripts data directory (see above). An existing one
# (e.g. installed with the Lobster modules that scripts need) is kept, and only a directory
# created here is removed again.
if(EXISTS "${scripts_dir}")
    set(created_scripts_dir FALSE)
    file(REMOVE "${dump}")
else()
    set(created_scripts_dir TRUE)
    file(COPY "${SCRIPTS_SRC}/" DESTINATION "${scripts_dir}")
endif()

# -d implies the single-instance check is skipped (src/tsapp.h), so this always runs its own
# instance rather than forwarding to one already running for this user.
# The application exits with a non-zero code after dumping, so the result is ignored.
execute_process(COMMAND "${TREESHEETS_EXE}" -d WORKING_DIRECTORY "${exe_dir}")

if(NOT EXISTS "${dump}")
    message(FATAL_ERROR "TreeSheets did not produce ${dump} (a display is required)")
endif()
file(COPY_FILE "${dump}" "${OUTPUT}")
if(created_scripts_dir)
    file(REMOVE_RECURSE "${scripts_dir}")
else()
    file(REMOVE "${dump}")
endif()
