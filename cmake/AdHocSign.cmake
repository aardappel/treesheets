# CPack pre-build script: ad-hoc sign the staged app bundle, so that it and its resources are sealed.
# Without a valid signature, Gatekeeper reports a downloaded app on Apple Silicon as damaged.

file(GLOB apps LIST_DIRECTORIES true "${CPACK_TEMPORARY_INSTALL_DIRECTORY}/*.app")
if(NOT apps)
    message(FATAL_ERROR "No app bundle to sign in ${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
endif()
foreach(app IN LISTS apps)
    execute_process(COMMAND codesign --force --sign - "${app}" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND codesign --verify --strict --verbose=2 "${app}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
