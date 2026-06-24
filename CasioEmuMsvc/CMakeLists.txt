cmake_minimum_required(VERSION 3.10...3.22)

project(CasioEmuMsvc)
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
option(CASIOEMU_CORE_WEB "Build the headless Emscripten/WebAssembly core API" OFF)

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
file(GLOB MAIN_SOURCES_CHIPSET "${SOURCE_DIR}/Chipset/*.cpp")
file(GLOB MAIN_SOURCES_CRASH "${SOURCE_DIR}/CrashHandler/*.cpp")
file(GLOB MAIN_SOURCES_EXT "${SOURCE_DIR}/Ext/*.cpp")
file(GLOB MAIN_SOURCES_EXT_CAM "${SOURCE_DIR}/Ext/cam/*.cc")
file(GLOB MAIN_SOURCES_ROP "${SOURCE_DIR}/Rop/*.cpp")
file(GLOB MAIN_SOURCES_GUI "${SOURCE_DIR}/Gui/*.cpp")
file(GLOB MAIN_SOURCES_IMGUI "${SOURCE_DIR}/Gui/imgui/*.cpp")
file(GLOB MAIN_SOURCES_JIT "${SOURCE_DIR}/JitU8/*.cpp")
file(GLOB MAIN_SOURCES_PERIPHERAL "${SOURCE_DIR}/Peripheral/*.cpp")
file(GLOB MAIN_SOURCES_PLUGIN "${SOURCE_DIR}/Plugin/*.cpp")
file(GLOB MAIN_SOURCES_STARTUP "${SOURCE_DIR}/StartupUi/*.cpp")
file(GLOB MAIN_SOURCES_WEB "${SOURCE_DIR}/Web/*.cpp")
file(GLOB MAIN_SOURCES_EXT_MM "${SOURCE_DIR}/Ext/*.mm")

if(CASIOEMU_WEB)
    list(FILTER MAIN_SOURCES_ROOT EXCLUDE REGEX "casioemu\\.cpp$")
    list(FILTER MAIN_SOURCES_EXT EXCLUDE REGEX "SysDialog\\.cpp$")
    if(CASIOEMU_CORE_WEB)
        list(FILTER MAIN_SOURCES_WEB EXCLUDE REGEX "web_main\\.cpp$")
    else()
        list(FILTER MAIN_SOURCES_WEB EXCLUDE REGEX "core_api\\.cpp$")
    endif()
    set(MAIN_SOURCES
        ${MAIN_SOURCES_ROOT}
        ${MAIN_SOURCES_MINIZ}
        ${MAIN_SOURCES_CHIPSET}
        ${MAIN_SOURCES_EXT}
        ${MAIN_SOURCES_EXT_CAM}
        ${SOURCE_DIR}/Gui/Hooks.cpp
        ${MAIN_SOURCES_PERIPHERAL}
        ${MAIN_SOURCES_WEB}
    )
else()
    set(MAIN_SOURCES
        ${MAIN_SOURCES_ROOT}
        ${MAIN_SOURCES_MINIZ}
        ${MAIN_SOURCES_CHIPSET}
        ${MAIN_SOURCES_CRASH}
        ${MAIN_SOURCES_EXT}
        ${MAIN_SOURCES_EXT_CAM}
        ${MAIN_SOURCES_EXT_MM}
        ${MAIN_SOURCES_GUI}
        ${MAIN_SOURCES_ROP}
        ${MAIN_SOURCES_IMGUI}
        ${MAIN_SOURCES_JIT}
        ${MAIN_SOURCES_PERIPHERAL}
        ${MAIN_SOURCES_PLUGIN}
        ${MAIN_SOURCES_STARTUP}
    )
endif()

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
            # 1. Thu thập TẤT CẢ các file bên trong Assets.xcassets (bao gồm .json và .png)
            file(GLOB_RECURSE IOS_ASSETS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/iOSResources/Assets.xcassets/*")
            
            # 2. Định nghĩa vị trí đóng gói cho các file asset này trong App Bundle
            set_source_files_properties(${IOS_ASSETS_FILES} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
    
            # Cấu hình tương tự cho LaunchScreen nếu có
            set(LAUNCH_SCREEN ${CMAKE_SOURCE_DIR}/MacResources/LaunchScreen.storyboard)
            set_source_files_properties(${LAUNCH_SCREEN} PROPERTIES
                MACOSX_PACKAGE_LOCATION "Resources"
            )
    
            # Kiểm tra khẩn cấp xem MAIN_SOURCES có bị trống do filter trước đó không
            if(NOT MAIN_SOURCES)
                message(FATAL_ERROR "MAIN_SOURCES is empty before creating iOS executable!")
            endif()
    
            # 3. Tạo executable bằng cách truyền danh sách file asset cụ thể thay vì folder
            add_executable(${TARGET_NAME}
                MACOSX_BUNDLE
                ${MAIN_SOURCES}
                ${IOS_ASSETS_FILES}
                ${LAUNCH_SCREEN}
            )
    
            # 4. Chỉ định cho Xcode biết tên của AppIcon set nằm trong .xcassets
            set_target_properties(${TARGET_NAME} PROPERTIES
                XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
                XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon"
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

# Apply include dirs to target (scoped) to reduce global includes and compiler parsing
target_include_directories(${TARGET_NAME} PRIVATE ${PROJECT_INCLUDE_DIRS})

if(CASIOEMU_WEB OR IOS)
    target_compile_definitions(${TARGET_NAME} PRIVATE DISABLE_SENTRY ASMJIT_NO_JIT)
    if(CASIOEMU_WEB)
        target_compile_definitions(${TARGET_NAME} PRIVATE CASIOEMU_WEB)
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
        target_compile_definitions(${TARGET_NAME} PRIVATE CASIOEMU_CORE_WEB CASIOEMU_DISABLE_RAM_IMAGE CASIOEMU_ENABLE_KEYBOARD_KI_WRITE)
        target_link_options(${TARGET_NAME} PRIVATE
            -fexceptions
            -sDISABLE_EXCEPTION_CATCHING=0
            -sUSE_SDL=2
            -sUSE_SDL_IMAGE=2
            "SHELL:-sSDL2_IMAGE_FORMATS=[\"png\"]"
            -sALLOW_MEMORY_GROWTH=1
            -sINITIAL_MEMORY=134217728
            -sEXIT_RUNTIME=0
            -sFORCE_FILESYSTEM=1
            -sMODULARIZE=1
            -sEXPORT_NAME=CasioEmuCoreModule
            "SHELL:-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','HEAPU8']"
            "SHELL:-sEXPORTED_FUNCTIONS=['_main','_malloc','_free','_casioemu_core_init_real_rom','_casioemu_core_init_sim_rom','_casioemu_core_shutdown','_casioemu_core_reset','_casioemu_core_step','_casioemu_core_is_running','_casioemu_core_cpu_time','_casioemu_core_key_mask','_casioemu_core_read_data','_casioemu_core_write_data','_casioemu_core_read_code','_casioemu_core_write_code','_casioemu_core_read_reg','_casioemu_core_write_reg','_casioemu_core_set_solar_voltage','_casioemu_core_set_battery_voltage','_casioemu_core_save_state','_casioemu_core_load_state']"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/locales@/locales"
            "SHELL:--preload-file ${CMAKE_CURRENT_SOURCE_DIR}/roms.db@/roms.db"
        )
        set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".js")
    else()
        target_link_options(${TARGET_NAME} PRIVATE
            -fexceptions
            -sDISABLE_EXCEPTION_CATCHING=0
            -sUSE_SDL=2
            -sUSE_SDL_IMAGE=2
            "SHELL:-sSDL2_IMAGE_FORMATS=[\"png\"]"
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
    if(WIN32)
        target_link_libraries(${TARGET_NAME} PRIVATE
            SDL2::SDL2main
            winmm
            dwmapi
            dbghelp
            imm32
        )
        set_target_properties(${TARGET_NAME} PROPERTIES PREFIX "")
    elseif(APPLE AND NOT IOS)
        target_link_libraries(${TARGET_NAME} PRIVATE
            "-framework Cocoa"
            "-framework OpenGL"
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
        )

        # Compile SysDialog.cpp as Objective-C++ for iOS native file picker
        set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/src/Ext/SysDialog.cpp PROPERTIES COMPILE_FLAGS "-x objective-c++")

        set_target_properties(${TARGET_NAME} PROPERTIES
            XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        )
    else()
        
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
