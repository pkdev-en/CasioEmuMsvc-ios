include(BundleUtilities)

get_filename_component(APP_BUNDLE "${APP}" ABSOLUTE)

message(STATUS "Fixing macOS bundle: ${APP_BUNDLE}")

if(NOT EXISTS "${APP_BUNDLE}")
    message(FATAL_ERROR "Bundle path does not exist: ${APP_BUNDLE}")
endif()

# Fix dylib dependencies
fixup_bundle("${APP_BUNDLE}" "" "")

message(STATUS "Bundle fix complete.")