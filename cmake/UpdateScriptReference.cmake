# Runs `TreeSheets -d` to dump the builtin function documentation and stores it as OUTPUT.
# Usage: cmake -DTREESHEETS_EXE=<exe> -DSCRIPTS_SRC=<TS/scripts> -DOUTPUT=<html> -P <this file>

cmake_path(GET TREESHEETS_EXE PARENT_PATH exe_dir)
set(scripts_dir "${exe_dir}/scripts")
set(dump "${scripts_dir}/builtin_functions_reference.html")

# The dump is written into the scripts directory next to the executable.
file(REMOVE_RECURSE "${scripts_dir}")
file(COPY "${SCRIPTS_SRC}/" DESTINATION "${scripts_dir}")

# The application exits with a non-zero code after dumping, so the result is ignored.
execute_process(COMMAND "${TREESHEETS_EXE}" -d WORKING_DIRECTORY "${exe_dir}")

if(NOT EXISTS "${dump}")
    message(FATAL_ERROR "TreeSheets did not produce ${dump} (a display is required)")
endif()
file(COPY_FILE "${dump}" "${OUTPUT}")
file(REMOVE_RECURSE "${scripts_dir}")
