# PackagePortable.cmake — Create a portable ZIP from the deploy directory
# Called by the package_portable custom target.

set(PACKAGE_NAME "OpenGeoStudio-${PACKAGE_VERSION}-Windows-x64")
set(ZIP_PATH "${OUTPUT_DIR}/${PACKAGE_NAME}.zip")

# Remove old ZIP if it exists
if(EXISTS "${ZIP_PATH}")
    file(REMOVE "${ZIP_PATH}")
endif()

# Create a temporary staging directory with the proper top-level folder name
set(STAGING_DIR "${OUTPUT_DIR}/${PACKAGE_NAME}")
if(EXISTS "${STAGING_DIR}")
    file(REMOVE_RECURSE "${STAGING_DIR}")
endif()
file(MAKE_DIRECTORY "${STAGING_DIR}")

# Copy all files from the deploy directory into the staging directory
file(COPY "${PACKAGE_DIR}/" DESTINATION "${STAGING_DIR}")

# Remove files that should not be in a portable package
file(REMOVE "${STAGING_DIR}/crash_diag.txt")
file(REMOVE "${STAGING_DIR}/log.txt")
file(REMOVE "${STAGING_DIR}/vc_redist.x64.exe")

# Create the ZIP using PowerShell's Compress-Archive
execute_process(
    COMMAND powershell -NoProfile -NonInteractive -Command
        "Compress-Archive -Path '${STAGING_DIR}/*' -DestinationPath '${ZIP_PATH}' -Force"
    RESULT_VARIABLE zip_result
)

# Clean up staging directory
file(REMOVE_RECURSE "${STAGING_DIR}")

if(zip_result EQUAL 0)
    message(STATUS "Portable package created: ${ZIP_PATH}")
else()
    message(FATAL_ERROR "Failed to create portable ZIP package (error: ${zip_result})")
endif()
