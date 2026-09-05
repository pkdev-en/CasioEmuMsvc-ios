cmake_minimum_required(VERSION 3.10...3.22)

project(CasioEmuMsvc)
include(FetchContent)
#set(CMAKE_MESSAGE_LOG_LEVEL NOTICE)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)

set(HAVE_ITOA 0 CACHE BOOL "" FORCE)
set(HAVE_UI64TOA 0 CACHE BOOL "" FORCE)
set(HAVE_I64TOA 0 CACHE BOOL "" FORCE)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU" AND NOT MSVC)
    add_compile_options(-Wno-error=implicit-function-declaration)
endif()

add_definitions(-D_GNU_SOURCE)
set(CMAKE_CXX_STANDARD_REQUIRED True)
option(CASIOEMU_WEB "Build the Emscripten/WebAssembly version" OFF)
option(CASIOEMU_CORE_WEB "Build the Emscripten/WebAssembly core API" OFF)
option(CASIOEMU_WEB_DEBUG_INFO "Enable heavier Emscripten runtime and source-map diagnostics" OFF)

if(CASIOEMU_CORE_WEB)
    set(CASIOEMU_WEB ON CACHE BOOL "Core web builds use the Emscripten/WebAssembly pipeline" FORCE)
endif()

if(EMSCRIPTEN OR CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    set(CASIOEMU_WEB ON CACHE BOOL "Build the Emscripten/WebAssembly version" FORCE)
    set(BUILD_EXECUTABLE ON CACHE BOOL "Web builds produce an executable html/js/wasm bundle" FORCE)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(IOS TRUE)
endif()

if(IOS)
    # Code signing handled per-target after the target is created below
    set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO")
endif()

#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fms-extensions")
# set(CMAKE_CXX_EXTENSIONS OFF)
set(SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(THIRD_PARTY_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set(ASMJIT_DIR "${THIRD_PARTY_DIR}/asmjit")
set(MINIZ_DIR "${THIRD_PARTY_DIR}/miniz")
set(TINYXML2_DIR "${THIRD_PARTY_DIR}/tinyxml2")
set(TEMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/temp")
set(GUI_FONT_SUBSET "${TEMP_DIR}/generated/fonts/CasioEmuGuiCJKSubset.otf")
file(GLOB GUI_FONT_LOCALE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/locales/*.lc")

if(NOT DEFINED CASIOEMU_ONLINE_BUILD_KEY_ID AND DEFINED ENV{CASIOEMU_ONLINE_BUILD_KEY_ID})
    set(CASIOEMU_ONLINE_BUILD_KEY_ID "$ENV{CASIOEMU_ONLINE_BUILD_KEY_ID}")
endif()
if(NOT DEFINED CASIOEMU_ONLINE_BUILD_KEY_B64 AND DEFINED ENV{CASIOEMU_ONLINE_BUILD_KEY_B64})
    set(CASIOEMU_ONLINE_BUILD_KEY_B64 "$ENV{CASIOEMU_ONLINE_BUILD_KEY_B64}")
endif()
if(NOT DEFINED CASIOEMU_ONLINE_SERVER_KEY_ID AND DEFINED ENV{CASIOEMU_ONLINE_SERVER_KEY_ID})
    set(CASIOEMU_ONLINE_SERVER_KEY_ID "$ENV{CASIOEMU_ONLINE_SERVER_KEY_ID}")
endif()
if(NOT DEFINED CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 AND DEFINED ENV{CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64})
    set(CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 "$ENV{CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64}")
endif()
set(CASIOEMU_ONLINE_BUILD_KEY_ID "${CASIOEMU_ONLINE_BUILD_KEY_ID}" CACHE STRING "Online API build key id")
set(CASIOEMU_ONLINE_BUILD_KEY_B64 "${CASIOEMU_ONLINE_BUILD_KEY_B64}" CACHE STRING "Online API build key in base64url form")
set(CASIOEMU_ONLINE_SERVER_KEY_ID "${CASIOEMU_ONLINE_SERVER_KEY_ID}" CACHE STRING "Online API server signing key id")
set(CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 "${CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64}" CACHE STRING "Online API Ed25519 server public key in base64url form")
if((CASIOEMU_ONLINE_BUILD_KEY_ID AND NOT CASIOEMU_ONLINE_BUILD_KEY_B64) OR
   (CASIOEMU_ONLINE_BUILD_KEY_B64 AND NOT CASIOEMU_ONLINE_BUILD_KEY_ID))
    message(FATAL_ERROR "Both CASIOEMU_ONLINE_BUILD_KEY_ID and CASIOEMU_ONLINE_BUILD_KEY_B64 must be configured together")
endif()
if(CASIOEMU_ONLINE_BUILD_KEY_ID)
    string(LENGTH "${CASIOEMU_ONLINE_BUILD_KEY_ID}" CASIOEMU_ONLINE_BUILD_KEY_ID_LENGTH)
    if(CASIOEMU_ONLINE_BUILD_KEY_ID_LENGTH GREATER 64 OR NOT CASIOEMU_ONLINE_BUILD_KEY_ID MATCHES "^[A-Za-z0-9._-]+$")
        message(FATAL_ERROR "CASIOEMU_ONLINE_BUILD_KEY_ID must be 1-64 characters using only letters, digits, dot, underscore, or hyphen")
    endif()
endif()
if(CASIOEMU_ONLINE_BUILD_KEY_B64)
    string(LENGTH "${CASIOEMU_ONLINE_BUILD_KEY_B64}" CASIOEMU_ONLINE_BUILD_KEY_LENGTH)
    if(CASIOEMU_ONLINE_BUILD_KEY_LENGTH LESS 43 OR NOT CASIOEMU_ONLINE_BUILD_KEY_B64 MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR "CASIOEMU_ONLINE_BUILD_KEY_B64 must be an unpadded base64url value containing at least 32 bytes")
    endif()
endif()
if((CASIOEMU_ONLINE_SERVER_KEY_ID AND NOT CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64) OR
   (CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 AND NOT CASIOEMU_ONLINE_SERVER_KEY_ID))
    message(FATAL_ERROR "Both CASIOEMU_ONLINE_SERVER_KEY_ID and CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 must be configured together")
endif()
if(CASIOEMU_ONLINE_SERVER_KEY_ID)
    string(LENGTH "${CASIOEMU_ONLINE_SERVER_KEY_ID}" CASIOEMU_ONLINE_SERVER_KEY_ID_LENGTH)
    if(CASIOEMU_ONLINE_SERVER_KEY_ID_LENGTH GREATER 64 OR NOT CASIOEMU_ONLINE_SERVER_KEY_ID MATCHES "^[A-Za-z0-9._-]+$")
        message(FATAL_ERROR "CASIOEMU_ONLINE_SERVER_KEY_ID must be 1-64 characters using only letters, digits, dot, underscore, or hyphen")
    endif()
endif()
if(CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64)
    string(LENGTH "${CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64}" CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_LENGTH)
    if(NOT CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_LENGTH EQUAL 43 OR NOT CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 MATCHES "^[A-Za-z0-9_-]+$")
        message(FATAL_ERROR "CASIOEMU_ONLINE_SERVER_PUBLIC_KEY_B64 must be an unpadded base64url Ed25519 public key containing exactly 32 bytes")
    endif()
endif()

function(casioemu_mask_base64url INPUT_VALUE OUTPUT_DATA OUTPUT_MASK OUTPUT_LENGTH)
    set(_alphabet "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")
    string(LENGTH "${INPUT_VALUE}" _length)
    if(_length EQUAL 0)
        set(${OUTPUT_DATA} "0" PARENT_SCOPE)
        set(${OUTPUT_MASK} "0" PARENT_SCOPE)
        set(${OUTPUT_LENGTH} "0" PARENT_SCOPE)
        return()
    endif()
    set(_data "")
    set(_masks "")
    set(_separator "")
    math(EXPR _last "${_length} - 1")
    foreach(_index RANGE 0 ${_last})
        string(SUBSTRING "${INPUT_VALUE}" ${_index} 1 _character)
        string(FIND "${_alphabet}" "${_character}" _character_index)
        if(_character_index LESS 0)
            message(FATAL_ERROR "Cannot mask non-base64url character in CASIOEMU_ONLINE_BUILD_KEY_B64")
        endif()
        string(RANDOM LENGTH 1 ALPHABET "${_alphabet}" _mask_character)
        string(FIND "${_alphabet}" "${_mask_character}" _mask_index)
        math(EXPR _masked_index "${_character_index} ^ ${_mask_index}")
        string(APPEND _data "${_separator}${_masked_index}")
        string(APPEND _masks "${_separator}${_mask_index}")
        set(_separator ", ")
    endforeach()
    set(${OUTPUT_DATA} "${_data}" PARENT_SCOPE)
    set(${OUTPUT_MASK} "${_masks}" PARENT_SCOPE)
    set(${OUTPUT_LENGTH} "${_length}" PARENT_SCOPE)
endfunction()

casioemu_mask_base64url("${CASIOEMU_ONLINE_BUILD_KEY_B64}"
    CASIOEMU_ONLINE_BUILD_KEY_MASKED_DATA
    CASIOEMU_ONLINE_BUILD_KEY_MASK_DATA
    CASIOEMU_ONLINE_BUILD_KEY_MASKED_LENGTH)
string(TIMESTAMP CASIOEMU_ONLINE_BUILD_TIMESTAMP "%Y%m%dT%H%M%SZ" UTC)
if(WIN32)
    set(CASIOEMU_ONLINE_BUILD_OS "Windows")
elseif(APPLE)
    set(CASIOEMU_ONLINE_BUILD_OS "macOS")
elseif(ANDROID)
    set(CASIOEMU_ONLINE_BUILD_OS "Android")
else()
    set(CASIOEMU_ONLINE_BUILD_OS "Linux")
endif()
if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(CASIOEMU_ONLINE_BUILD_ARCH "x86_64")
else()
    set(CASIOEMU_ONLINE_BUILD_ARCH "x86")
endif()
if(ANDROID_ABI)
    set(CASIOEMU_ONLINE_BUILD_ARCH "${ANDROID_ABI}")
elseif(CMAKE_OSX_ARCHITECTURES MATCHES "arm64" AND CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
    set(CASIOEMU_ONLINE_BUILD_ARCH "universal")
elseif(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
    set(CASIOEMU_ONLINE_BUILD_ARCH "arm64")
endif()
set(ONLINE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ONLINE_GENERATED_DIR}")
execute_process(COMMAND git describe --tags --exact-match HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE CASIOEMU_GIT_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT CASIOEMU_GIT_REF)
    execute_process(COMMAND git branch --show-current WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        OUTPUT_VARIABLE CASIOEMU_GIT_REF OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
execute_process(COMMAND git rev-parse --short HEAD WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE CASIOEMU_GIT_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
execute_process(COMMAND git log -1 --format=%cd --date=format:%Y-%m-%d WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE CASIOEMU_GIT_COMMIT_DATE OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT CASIOEMU_GIT_REF)
    set(CASIOEMU_GIT_REF "detached")
endif()
string(REGEX REPLACE "[^A-Za-z0-9._-]" "_" CASIOEMU_GIT_REF "${CASIOEMU_GIT_REF}")
if(NOT CASIOEMU_GIT_COMMIT)
    set(CASIOEMU_GIT_COMMIT "0000000")
endif()
if(NOT CASIOEMU_GIT_COMMIT_DATE)
    set(CASIOEMU_GIT_COMMIT_DATE "1970-01-01")
endif()
configure_file(
    "${SOURCE_DIR}/StartupUi/OnlineBuildConfig.h.in"
    "${ONLINE_GENERATED_DIR}/OnlineBuildConfig.h"
    @ONLY
)
configure_file(
    "${SOURCE_DIR}/git_info.h.in"
    "${ONLINE_GENERATED_DIR}/git_info.h"
    @ONLY
)

set(ASMJIT_STATIC ON CACHE BOOL "Build AsmJit as a static library")
set(ASMJIT_BUILD_TEST OFF CACHE BOOL "Disable AsmJit tests")
set(ASMJIT_BUILD_EXAMPLES OFF CACHE BOOL "Disable AsmJit examples")

if(NOT CASIOEMU_WEB AND NOT IOS)
    message(STATUS "Attempting to add subdirectory: ${ASMJIT_DIR}")
    if(EXISTS "${ASMJIT_DIR}/CMakeLists.txt")
        message(STATUS "Found ${ASMJIT_DIR}/CMakeLists.txt")
        add_subdirectory(${ASMJIT_DIR})
        message(STATUS "Finished add_subdirectory(asmjit)")
    else()
        message(FATAL_ERROR "Could not find asmjit/CMakeLists.txt at ${ASMJIT_DIR}")
    endif()

    if(TARGET asmjit::asmjit)
        message(STATUS "Target asmjit::asmjit FOUND after add_subdirectory")
    else()
        message(WARNING "Target asmjit::asmjit NOT FOUND after add_subdirectory")
    endif()
else()
    message(STATUS "Web/Mobile build: skipping AsmJit")
endif()

if(NOT ANDROID AND NOT IOS AND NOT CASIOEMU_WEB)
    set(SENTRY_BUILD_SHARED_LIBS OFF CACHE BOOL "Disable Sentry shared lib" FORCE)
    set(SENTRY_BUILD_RUNTIMESTATIC ${BUILD_EXECUTABLE} CACHE BOOL "Static runtime for Sentry (MSVC /MT)" FORCE)
    set(SENTRY_BUILD_TESTS OFF CACHE BOOL "Disable Sentry tests" FORCE)
    set(SENTRY_BUILD_EXAMPLES OFF CACHE BOOL "Disable Sentry examples" FORCE)
    add_subdirectory(${CMAKE_SOURCE_DIR}/sentry-native sentry-native-build)
endif()

# Collect project include directories into a variable; will be applied to the target only (not globally)
set(PROJECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${SOURCE_DIR}
    ${SOURCE_DIR}/Chipset
    ${SOURCE_DIR}/Chipset/EPS6800Core
    ${SOURCE_DIR}/Containers
    ${SOURCE_DIR}/CrashHandler
    ${SOURCE_DIR}/Ext
    ${SOURCE_DIR}/Ext/cam
    ${SOURCE_DIR}/Gui
    ${SOURCE_DIR}/Gui/imgui
    ${SOURCE_DIR}/JitU8
    ${SOURCE_DIR}/Rop
    ${SOURCE_DIR}/Peripheral
    ${SOURCE_DIR}/Plugin
    ${SOURCE_DIR}/StartupUi
    ${SOURCE_DIR}/locales
    ${THIRD_PARTY_DIR}
    ${ASMJIT_DIR}/src
    ${MINIZ_DIR}
)

# find_library(SDL2 SDL2)

# find_library(SDL2_image SDL2_image)

# add_library(main SHARED)

# Search for all .cpp files in the CasioEmuMsvc directory 
# file(GLOB_RECURSE SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

# Use individual directories instead of all to avoid asmjit's file testing
file(GLOB MAIN_SOURCES_ROOT "${SOURCE_DIR}/*.cpp")
file(GLOB MAIN_SOURCES_MINIZ "${MINIZ_DIR}/*.c")
file(GLOB MAIN_SOURCES_TINYXML2 "${TINYXML2_DIR}/*.cpp")
file(GLOB MAIN_SOURCES_CHIPSET "${SOURCE_DIR}/Chipset/*.cpp")
file(GLOB MAIN_SOURCES_EPS6800_CORE "${SOURCE_DIR}/Chipset/EPS6800Core/*.c")
file(GLOB MAIN_SOURCES_CRASH "${SOURCE_DIR}/CrashHandler/*.cpp")
file(GLOB MAIN_SOURCES_EXT "${SOURCE_DIR}/Ext/*.cpp" "${SOURCE_DIR}/Ext/*.mm")
file(GLOB MAIN_SOURCES_EXT_CAM "${SOURCE_DIR}/Ext/cam/*.cc")
file(GLOB MAIN_SOURCES_ROP "${SOURCE_DIR}/Rop/*.cpp")
file(GLOB MAIN_SOURCES_GUI "${SOURCE_DIR}/Gui/*.cpp")
file(GLOB MAIN_SOURCES_IMGUI "${SOURCE_DIR}/Gui/imgui/*.cpp")
file(GLOB MAIN_SOURCES_JIT "${SOURCE_DIR}/JitU8/*.cpp")
file(GLOB MAIN_SOURCES_PERIPHERAL "${SOURCE_DIR}/Peripheral/*.cpp")
file(GLOB MAIN_SOURCES_PLUGIN "${SOURCE_DIR}/Plugin/*.cpp")
file(GLOB MAIN_SOURCES_STARTUP CONFIGURE_DEPENDS "${SOURCE_DIR}/StartupUi/*.cpp")
file(GLOB MAIN_SOURCES_WEB "${SOURCE_DIR}/Web/*.cpp")
file(GLOB MAIN_SOURCES_EXT_MM "${SOURCE_DIR}/Ext/*.mm")

if(CASIOEMU_WEB)
    list(FILTER MAIN_SOURCES_ROOT EXCLUDE REGEX "casioemu\\.cpp$")
    list(FILTER MAIN_SOURCES_EXT EXCLUDE REGEX "SysDialog\\.cpp$")
    list(FILTER MAIN_SOURCES_EXT EXCLUDE REGEX "DiscordRPC\\.cpp$")
    if(CASIOEMU_CORE_WEB)
        list(FILTER MAIN_SOURCES_WEB EXCLUDE REGEX "web_main\\.cpp$")
    else()
        list(FILTER MAIN_SOURCES_WEB EXCLUDE REGEX "core_api\\.cpp$")
    endif()
    set(MAIN_SOURCES
        ${MAIN_SOURCES_ROOT}
        ${MAIN_SOURCES_MINIZ}
        ${MAIN_SOURCES_TINYXML2}
        ${MAIN_SOURCES_CHIPSET}
        ${MAIN_SOURCES_EXT}
        ${MAIN_SOURCES_EXT_CAM}
        ${SOURCE_DIR}/Gui/Hooks.cpp
        ${MAIN_SOURCES_PERIPHERAL}
        ${MAIN_SOURCES_WEB}
    )
    if(CASIOEMU_CORE_WEB)
        list(APPEND MAIN_SOURCES
            ${MAIN_SOURCES_IMGUI}
            ${SOURCE_DIR}/Gui/5800FileSystem.cpp
            ${SOURCE_DIR}/Gui/AddressWindow.cpp
            ${SOURCE_DIR}/Gui/BitmapViewer.cpp
            ${SOURCE_DIR}/Gui/CallAnalysis.cpp
            ${SOURCE_DIR}/Gui/CodeViewer.cpp
            ${SOURCE_DIR}/Gui/Editors.cpp
            ${SOURCE_DIR}/Gui/FileDialog.cpp
            ${SOURCE_DIR}/Gui/HwController.cpp
            ${SOURCE_DIR}/Gui/Injector.cpp
            ${SOURCE_DIR}/Gui/LabelViewer.cpp
            ${SOURCE_DIR}/Gui/MemBreakPoint.cpp
            ${SOURCE_DIR}/Gui/SnapshotWindow.cpp
            ${SOURCE_DIR}/Gui/Theme.cpp
            ${SOURCE_DIR}/Gui/ThemeManager.cpp
            ${SOURCE_DIR}/Gui/Ui.cpp
            ${SOURCE_DIR}/Gui/VariableWindow.cpp
            ${SOURCE_DIR}/Gui/WatchWindow.cpp
            ${SOURCE_DIR}/Gui/Localization.cpp
            ${MAIN_SOURCES_ROP}
        )
    endif()
else()
    # .mm (Objective-C++) files only compile on Apple platforms (iOS/macOS)
    if(APPLE)
        set(APPLE_MM_SOURCES ${MAIN_SOURCES_EXT_MM})
    else()
        set(APPLE_MM_SOURCES "")
    endif()
    set(MAIN_SOURCES
        ${MAIN_SOURCES_ROOT}
        ${MAIN_SOURCES_MINIZ}
        ${MAIN_SOURCES_TINYXML2}
        ${MAIN_SOURCES_CHIPSET}
        ${MAIN_SOURCES_CRASH}
        ${MAIN_SOURCES_EXT}
        ${MAIN_SOURCES_EXT_CAM}
        ${APPLE_MM_SOURCES}
        ${MAIN_SOURCES_GUI}
        ${MAIN_SOURCES_ROP}
        ${MAIN_SOURCES_IMGUI}
        ${MAIN_SOURCES_JIT}
        ${MAIN_SOURCES_PERIPHERAL}
        ${MAIN_SOURCES_PLUGIN}
        ${MAIN_SOURCES_STARTUP}
    )
endif()

list(APPEND MAIN_SOURCES ${MAIN_SOURCES_EPS6800_CORE})

# target_sources(main PRIVATE ${MAIN_SOURCES})

if(BUILD_EXECUTABLE)
    message(STATUS "Building executable")
    set(TARGET_NAME "CasioEmuMsvc")
    if(NOT MAIN_SOURCES)
        message(FATAL_ERROR "MAIN_SOURCES is EMPTY on this platform 💀")
    endif()

    if(APPLE)
        set(TARGET_NAME "CasioEmuMsvc")
    
        if(IOS)
            # 1. Copy PNG icon trực tiếp vào bundle (không dùng xcassets)
            set(IOS_ICON_DIR "${CMAKE_SOURCE_DIR}/iOSresources/Assets.xcassets/AppIcon.appiconset")
            set(IOS_ICON_FILES
                "${IOS_ICON_DIR}/180.png"
                "${IOS_ICON_DIR}/120.png"
                "${IOS_ICON_DIR}/87.png"
                "${IOS_ICON_DIR}/80.png"
                "${IOS_ICON_DIR}/60.png"
                "${IOS_ICON_DIR}/58.png"
                "${IOS_ICON_DIR}/40.png"
                "${IOS_ICON_DIR}/29.png"
                "${IOS_ICON_DIR}/20.png"
                "${IOS_ICON_DIR}/1024.png"
            )
            set_source_files_properties(${IOS_ICON_FILES} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )

            # 2. Cấu hình LaunchScreen
            set(LAUNCH_SCREEN ${CMAKE_SOURCE_DIR}/MacResources/LaunchScreen.storyboard)
            set_source_files_properties(${LAUNCH_SCREEN} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )

            # Kiểm tra khẩn cấp xem MAIN_SOURCES có bị trống do filter trước đó không
            if(NOT MAIN_SOURCES)
                message(FATAL_ERROR "MAIN_SOURCES is empty before creating iOS executable!")
            endif()

            # 2b. GUI shell riêng cho iOS (không dùng chung code cửa sổ desktop) —
            # port từ app/src/main/java/com/tele/u8emulator/Game.java
            set(IOS_GUI_DIR "${CMAKE_SOURCE_DIR}/iOSresources/Sources")
            set(IOS_GUI_SOURCES
                "${IOS_GUI_DIR}/game_controller_core.cpp"
                "${IOS_GUI_DIR}/GameViewController.mm"
            )
            set(IOS_GUI_HEADERS
                "${IOS_GUI_DIR}/game_controller_core.hpp"
                "${IOS_GUI_DIR}/GameViewController.h"
            )
            # .mm cần Objective-C ARC bật riêng (không áp dụng cho các file .cpp/.mm khác)
            set_source_files_properties(${IOS_GUI_DIR}/GameViewController.mm PROPERTIES
                COMPILE_FLAGS "-fobjc-arc"
            )

            # 3. Tạo executable với icon PNG trực tiếp
            add_executable(${TARGET_NAME}
                MACOSX_BUNDLE
                ${MAIN_SOURCES}
                ${IOS_GUI_SOURCES}
                ${IOS_GUI_HEADERS}
                ${IOS_ICON_FILES}
                ${LAUNCH_SCREEN}
            )

            target_include_directories(${TARGET_NAME} PRIVATE ${IOS_GUI_DIR})

            # 4. Deployment target
            set_target_properties(${TARGET_NAME} PROPERTIES
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
            )

            # 5. Framework UIKit cần cho GameViewController.mm (UNUserNotifications, Photos)
            target_link_libraries(${TARGET_NAME} PRIVATE
                "-framework UIKit"
                "-framework UserNotifications"
                "-framework Photos"
                "-framework CoreGraphics"
            )
    
        else()
            # Cấu hình giữ nguyên cho macOS
            set(APP_ICON ${CMAKE_SOURCE_DIR}/MacResources/AppIcon.icns)
            set_source_files_properties(${APP_ICON} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
            set(LAUNCH_SCREEN ${CMAKE_SOURCE_DIR}/MacResources/LaunchScreen.storyboard)
            set_source_files_properties(${LAUNCH_SCREEN} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
    
            add_executable(${TARGET_NAME}
                MACOSX_BUNDLE
                ${MAIN_SOURCES}
                ${APP_ICON}
                ${LAUNCH_SCREEN}
            )
        endif()



    else()
        if(CASIOEMU_CORE_WEB)
            set(TARGET_NAME "CasioEmuCore")
        elseif(CASIOEMU_WEB)
            set(TARGET_NAME "CasioEmuWeb")
        

        elseif(WIN32)
            set(WIN_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/Resource.rc)
        endif()

        add_executable(${TARGET_NAME}
            ${MAIN_SOURCES}
            ${WIN_RESOURCES}
        )
    endif()



else()
    message(STATUS "Building shared library")
    set(TARGET_NAME "main")
    add_library(${TARGET_NAME} SHARED ${MAIN_SOURCES})
endif()

if(APPLE AND NOT IOS)
    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST
        ${CMAKE_SOURCE_DIR}/MacResources/Info.plist
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.pkdevvn.casioemu"

        INSTALL_RPATH "@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
elseif(IOS)
    set_target_properties(${TARGET_NAME} PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST
        ${CMAKE_SOURCE_DIR}/iOSResources/Info.plist
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.pkdevvn.casioemu"

        INSTALL_RPATH "@executable_path/Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
    )
endif()

if(CASIOEMU_CORE_WEB)
    find_package(Python3 COMPONENTS Interpreter)
    if(NOT Python3_Interpreter_FOUND)
        message(FATAL_ERROR "Python3 is required to build the web GUI font subset")
    endif()
    add_custom_command(
        OUTPUT "${GUI_FONT_SUBSET}"
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_gui_font_subset.py"
            "--project-root" "${CMAKE_CURRENT_SOURCE_DIR}"
            "--output" "${GUI_FONT_SUBSET}"
            "--python" "${Python3_EXECUTABLE}"
        DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/build_gui_font_subset.py"
            ${GUI_FONT_LOCALE_SOURCES}
        COMMENT "Building bundled ImGui CJK fallback font subset"
        VERBATIM
    )
    add_custom_target(CasioEmuGuiFontSubset DEPENDS "${GUI_FONT_SUBSET}")
    add_dependencies(${TARGET_NAME} CasioEmuGuiFontSubset)
endif()

# Apply include dirs to target (scoped) to reduce global includes and compiler parsing
target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_INCLUDE_DIRS})
target_include_directories(${TARGET_NAME} BEFORE PRIVATE "${ONLINE_GENERATED_DIR}")

if(NOT CASIOEMU_WEB)
    FetchContent_Declare(monocypher
        GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
        GIT_TAG ab2b16dd619ad5f6979a4fbe69cfa324a6fcc35f
    )
    FetchContent_MakeAvailable(monocypher)
    target_sources(${TARGET_NAME} PRIVATE
        "${monocypher_SOURCE_DIR}/src/monocypher.c"
        "${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c"
    )
    target_include_directories(${TARGET_NAME} PRIVATE
        "${monocypher_SOURCE_DIR}/src"
        "${monocypher_SOURCE_DIR}/src/optional"
    )
endif()

if(CASIOEMU_WEB OR IOS)
    target_compile_definitions(${TARGET_NAME} PRIVATE DISABLE_SENTRY ASMJIT_NO_JIT)
    if(CASIOEMU_WEB)
        target_compile_definitions(${TARGET_NAME} PRIVATE CASIOEMU_WEB)
    endif()
    if(IOS)
        target_compile_definitions(${TARGET_NAME} PRIVATE __IOS__)
    endif()
else()
    target_compile_definitions(${TARGET_NAME} PRIVATE ASMJIT_STATIC)
endif()

set(PLATFORM_LIBS "")

if(WIN32)
    #target_link_options(${TARGET_NAME} PRIVATE "-mconsole")
    if(NOT MSVC AND NOT APPLE)
        target_link_options(${TARGET_NAME} PRIVATE
            "-static-libgcc"
            "-static-libstdc++"
            "-static"
        )
    endif()
    target_compile_definitions(${TARGET_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(MSVC)
    # Ensure UTF-8 and increase compiler heap and object limits for large translation units
    target_compile_options(${TARGET_NAME} PRIVATE /utf-8 /Zm200 /bigobj)
endif()

if(CASIOEMU_WEB)
    target_compile_options(${TARGET_NAME} PRIVATE
        -fexceptions
        -sUSE_SDL=2
        -sUSE_SDL_IMAGE=2
    )
    if(CASIOEMU_CORE_WEB)
        target_compile_options(${TARGET_NAME} PRIVATE
            -pthread
        )
        target_compile_definitions(${TARGET_NAME} PRIVATE CASIOEMU_CORE_WEB CASIOEMU_DISABLE_RAM_IMAGE CASIOEMU_ENABLE_KEYBOARD_KI_WRITE)
        target_link_options(${TARGET_NAME} PRIVATE
            -pthread
            -fexceptions
            -sDISABLE_EXCEPTION_CATCHING=0
            -sUSE_PTHREADS=1
            -sPTHREAD_POOL_SIZE=4
            -sDEFAULT_PTHREAD_STACK_SIZE=8388608
            -sUSE_SDL=2
            -sUSE_SDL_IMAGE=2
            "SHELL:-sSDL2_IMAGE_FORMATS=[\"png\"]"
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=134217728
            -sSTACK_SIZE=8388608
            -sEXIT_RUNTIME=0
            -sFORCE_FILESYSTEM=1
            -sMODULARIZE=1
            -sEXPORT_NAME=CasioEmuCoreModule
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','ccall','cwrap','HEAPU8']"
            "SHELL:-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_casioemu_core_set_model_id','_casioemu_core_init_real_rom','_casioemu_core_init_real_rom_with_flash','_casioemu_core_init_sim_rom','_casioemu_core_shutdown','_casioemu_core_start','_casioemu_core_stop','_casioemu_core_reset','_casioemu_core_is_running','_casioemu_core_cpu_time','_casioemu_core_qr_update','_casioemu_core_qr_active','_casioemu_core_qr_version','_casioemu_core_qr_revision','_casioemu_core_qr_data','_casioemu_core_button_event','_casioemu_core_sdl_key_event','_casioemu_core_bind_sdl_key','_casioemu_core_set_display_color','_casioemu_core_set_screen_shadow','_casioemu_core_update_frame','_casioemu_core_frame_ptr','_casioemu_core_frame_width','_casioemu_core_frame_height','_casioemu_core_status_alpha_ptr','_casioemu_core_status_alpha_len','_casioemu_core_set_solar_voltage','_casioemu_core_set_battery_voltage','_casioemu_core_persistent_ram_size','_casioemu_core_save_persistent_ram','_casioemu_core_load_persistent_ram','_casioemu_core_snapshot_ptr','_casioemu_core_snapshot_len','_casioemu_core_save_snapshot','_casioemu_core_load_snapshot','_casioemu_core_rom_version','_casioemu_core_gui_supported','_casioemu_core_gui_attach','_casioemu_core_gui_detach','_casioemu_core_gui_is_attached','_casioemu_core_gui_frame','_casioemu_core_gui_frame_ptr','_casioemu_core_gui_frame_len','_casioemu_core_gui_width','_casioemu_core_gui_height','_casioemu_core_gui_set_locale','_casioemu_core_gui_pointer','_casioemu_core_gui_wheel','_casioemu_core_gui_key','_casioemu_core_gui_text','_casioemu_core_gui_want_text_input','_casioemu_core_gui_ime_visible','_casioemu_core_gui_ime_x','_casioemu_core_gui_ime_y','_casioemu_core_gui_ime_line_height','_casioemu_core_gui_set_clipboard_text','_casioemu_core_gui_clipboard_write_pending','_casioemu_core_gui_clipboard_write_text','_casioemu_core_gui_clipboard_write_revision','_casioemu_core_gui_clipboard_write_ack','_casioemu_core_gui_file_request_pending','_casioemu_core_gui_file_request_kind','_casioemu_core_gui_file_request_path','_casioemu_core_gui_file_request_name','_casioemu_core_gui_file_request_ack','_casioemu_core_gui_file_request_complete']"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/locales@/locales"
            "SHELL:--preload-file ${GUI_FONT_SUBSET}@/fonts/CasioEmuGuiCJKSubset.otf"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/roms.db@/roms.db"
            -lidbfs.js
        )
        if(CASIOEMU_WEB_DEBUG_INFO)
            target_compile_options(${TARGET_NAME} PRIVATE
                -gsource-map
                -fno-omit-frame-pointer
            )
            target_link_options(${TARGET_NAME} PRIVATE
                -gsource-map
                --profiling-funcs
                -sASSERTIONS=2
                -sSAFE_HEAP=1
                -sSTACK_OVERFLOW_CHECK=2
            )
        endif()
        set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".js")
    else()
        target_link_options(${TARGET_NAME} PRIVATE
            -fexceptions
            -sDISABLE_EXCEPTION_CATCHING=0
            -sUSE_SDL=2
            -sUSE_SDL_IMAGE=2
            "SHELL:-sSDL2_IMAGE_FORMATS=[\"png\",\"svg\"]"
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=134217728
            -sEXIT_RUNTIME=0
            -sFORCE_FILESYSTEM=1
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','IDBFS','ccall','cwrap','UTF8ToString']"
            "SHELL:-sEXPORTED_FUNCTIONS=['_main','_casioemu_web_start','_casioemu_web_stop','_casioemu_web_pause','_casioemu_web_reset','_casioemu_web_resize','_casioemu_web_syncfs']"
            --shell-file
            "${CMAKE_CURRENT_SOURCE_DIR}/web/shell.html"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/locales@/locales"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/roms.db@/roms.db"
            -lidbfs.js
        )
        set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".html")
    endif()
elseif(BUILD_EXECUTABLE)
    target_link_libraries(${TARGET_NAME} PRIVATE
        SDL2::SDL2main
        SDL2::SDL2
        SDL2_image::SDL2_image-static
        #CpuFeatures::cpu_features
    )
    if(NOT IOS)
        target_link_libraries(${TARGET_NAME} PRIVATE asmjit::asmjit)
    endif()

    find_package(CURL QUIET)
    if(NOT CURL_FOUND)
        include(FetchContent)
        set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        if(WIN32)
            set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
        endif()
        set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
        set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
        set(CURL_ZLIB OFF CACHE STRING "" FORCE)
        set(CURL_DISABLE_LDAP ON CACHE BOOL "" FORCE)
        set(CURL_DISABLE_LDAPS ON CACHE BOOL "" FORCE)
        FetchContent_Declare(curl
            GIT_REPOSITORY https://github.com/curl/curl.git
            GIT_TAG curl-8_11_1
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(curl)
    endif()
    target_link_libraries(${TARGET_NAME} PRIVATE CURL::libcurl)
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE
            SDL2::SDL2main
            winmm
            dwmapi
            dbghelp
            imm32
            crypt32
            bcrypt
            ws2_32
        )
        set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "")
    elseif(APPLE AND NOT IOS)
        find_library(APPKIT_FRAMEWORK AppKit)
        find_library(SECURITY_FRAMEWORK Security REQUIRED)
        target_link_libraries(${TARGET_NAME} PRIVATE
            "-framework Cocoa"
            "-framework OpenGL"
            -lpthread
            -ldl
            -lm
            ${APPKIT_FRAMEWORK}
            ${SECURITY_FRAMEWORK}
        )
    elseif(IOS)
        target_link_libraries(${TARGET_NAME} PRIVATE
            "-framework Foundation"
            "-framework UIKit"
            "-framework CoreGraphics"
            "-framework QuartzCore"
            "-framework Metal"
            "-framework ImageIO"
            "-framework MobileCoreServices"
            "-framework UniformTypeIdentifiers"
            "-framework SafariServices"
        )

        # Compile SysDialog.cpp as Objective-C++ for iOS native file picker
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/Ext/SysDialog.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")

        set_target_properties(${TARGET_NAME} PROPERTIES
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        )
        )
    else()
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(LIBSECRET QUIET IMPORTED_TARGET libsecret-1)
            if(LIBSECRET_FOUND)
                target_link_libraries(${TARGET_NAME} PRIVATE PkgConfig::LIBSECRET)
                target_compile_definitions(${TARGET_NAME} PRIVATE CASIOEMU_HAS_LIBSECRET=1)
            endif()
        endif()
        target_link_libraries(${TARGET_NAME} PRIVATE
            -Wl,--start-group
            -lpthread
            -ldl
            -lrt
            -lm
            -Wl,--end-group
        )
    endif()
else()
    target_link_libraries(${TARGET_NAME} PRIVATE
        SDL2::SDL2
        SDL2_image::SDL2_image
        #CpuFeatures::cpu_features
    )
    if(NOT IOS)
        target_link_libraries(${TARGET_NAME} PRIVATE asmjit::asmjit)
    endif()
    if(ANDROID)
        target_link_libraries(${TARGET_NAME} PRIVATE log mediandk)
    endif()
endif()

if(NOT ANDROID AND NOT IOS AND NOT CASIOEMU_WEB)
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/discord-rpc/include)
    target_link_libraries(${TARGET_NAME} PRIVATE discord-rpc sentry::sentry)
endif()

file(GLOB LOCALE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/locales/*.lc")

if(ANDROID)
    set(ASSET_DIR ${CMAKE_SOURCE_DIR}/app/src/main/assets)
elseif(APPLE AND BUILD_EXECUTABLE AND NOT IOS)
    set(ASSET_DIR $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Resources)
elseif(IOS AND BUILD_EXECUTABLE)
    set(ASSET_DIR $<TARGET_BUNDLE_DIR:${TARGET_NAME}>)
elseif(BUILD_EXECUTABLE AND NOT CASIOEMU_WEB)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:${TARGET_NAME}>/locales)
    foreach(FILE_PATH ${LOCALE_FILES})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FILE_PATH}
            $<TARGET_FILE_DIR:${TARGET_NAME}>/locales/)
    endforeach()
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/roms.db
        $<TARGET_FILE_DIR:${TARGET_NAME}>/roms.db || ${CMAKE_COMMAND} -E true)
else()
    set(ASSET_DIR $<TARGET_FILE_DIR:${TARGET_NAME}>)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_SOURCE_DIR}/app/src/main/assets/locales)
    foreach(FILE_PATH ${LOCALE_FILES})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${FILE_PATH}
            ${CMAKE_SOURCE_DIR}/app/src/main/assets/locales/)
    endforeach()
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_CURRENT_SOURCE_DIR}/roms.db
        ${CMAKE_SOURCE_DIR}/app/src/main/assets/roms.db || ${CMAKE_COMMAND} -E true)
endif()

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    ${ASSET_DIR}/locales
)

if(NOT WIN32 AND NOT ANDROID AND NOT IOS AND NOT APPLE AND NOT CASIOEMU_WEB)
    if(BUILD_EXECUTABLE)
        target_link_options(${TARGET_NAME} PRIVATE "-static-libgcc" "-static-libstdc++")
    endif()
endif()

if(NOT ANDROID AND NOT IOS AND NOT CASIOEMU_WEB)
    if(TARGET crashpad_handler)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:crashpad_handler>
            $<TARGET_FILE_DIR:${TARGET_NAME}>/
        )
    endif()
endif()

if(APPLE AND BUILD_EXECUTABLE AND NOT IOS)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Resources/locales
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/locales
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Resources/locales
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Resources/fonts
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Resources/fonts
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Resources/fonts_cjk
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts_cjk
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/Contents/Resources/fonts_cjk
    )
elseif(IOS AND BUILD_EXECUTABLE)
    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/locales
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/locales
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/locales
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/fonts
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/fonts
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/fonts_cjk
    )

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/fonts_cjk
        $<TARGET_BUNDLE_DIR:${TARGET_NAME}>/fonts_cjk
    )
endif()

set(CPACK_GENERATOR "DragNDrop")
include(CPack)
