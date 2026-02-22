cmake_minimum_required(VERSION 3.20)

project(BubiC-8801MA LANGUAGES CXX)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS deployment target" FORCE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build universal binary" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC Runtime Static Linking (Avoids "VCRUNTIME140.dll not found")
# Must be set BEFORE add_executable()
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Configure sources
set(COMMON_SOURCES
    src/common.cpp
    src/config.cpp
    src/debugger.cpp
    src/emu.cpp
    src/fifo.cpp
    src/fileio.cpp
)

set(VM_SOURCES
    src/vm/ay_3_891x.cpp
    src/vm/disk.cpp
    src/vm/event.cpp
    src/vm/fmgen/fmgen.cpp
    src/vm/fmgen/fmtimer.cpp
    src/vm/fmgen/opm.cpp
    src/vm/fmgen/opna.cpp
    src/vm/fmgen/psg.cpp
    src/vm/i8251.cpp
    src/vm/i8253.cpp
    src/vm/i8255.cpp
    src/vm/noise.cpp
    src/vm/pc80s31k.cpp
    src/vm/pc8801/diskio.cpp
    src/vm/pc8801/pc88.cpp
    src/vm/pc8801/pc8801.cpp
    src/vm/pcm1bit.cpp
    src/vm/pcm8bit.cpp
    src/vm/prnfile.cpp
    src/vm/scsi_cdrom.cpp
    src/vm/scsi_dev.cpp
    src/vm/scsi_host.cpp
    src/vm/upd1990a.cpp
    src/vm/upd765a.cpp
    src/vm/ym2151.cpp
    src/vm/ym2203.cpp
    src/vm/z80.cpp
)

set(SDL_SOURCES
    src/sdl3/main.cpp
    src/sdl3/osd.cpp
)

# =====================================
# SDL3 Configuration (Platform Policy)
# Linux / macOS : build from source
# Windows       : use official binary
# =====================================

if(WIN32)
    # Windows: use prebuilt SDL3 (DLL)
    find_package(SDL3 REQUIRED CONFIG)
    message(STATUS "Using prebuilt SDL3 on Windows.")
else()
    # Linux / macOS: build SDL3 from source (static)
    include(FetchContent)

    FetchContent_Declare(
        SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-3.4.2
        GIT_SHALLOW    TRUE
    )

    # Static link (self-contained build)
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON  CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_SAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_MakeAvailable(SDL3)

    if(NOT TARGET SDL3::SDL3)
        add_library(SDL3::SDL3 ALIAS SDL3-static)
    endif()

    message(STATUS "SDL3 fetched and built statically (Linux/macOS).")
endif()

# Dear ImGui
include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.6
    # GIT_TAG        v1.91.8-docking # Use a stable tag
)
FetchContent_MakeAvailable(imgui)

# Add ImGui sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)

# Icon
set(ICON_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.icns)
set(ICON_ICO  ${CMAKE_CURRENT_SOURCE_DIR}/assets/AppIcon.ico)

# Windows resource file (embeds .ico)
if(WIN32)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/app.rc.in
        ${CMAKE_CURRENT_BINARY_DIR}/app.rc
        @ONLY
    )
    set(WIN_RC ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

# Executable
add_executable(BubiC-8801MA
    src/stdafx.cpp # Add stdafx.cpp to the sources
    ${COMMON_SOURCES}
    ${VM_SOURCES}
    ${SDL_SOURCES}
    ${IMGUI_SOURCES}
    $<$<BOOL:${APPLE}>:${ICON_ICNS}>
    $<$<BOOL:${WIN32}>:${WIN_RC}>
)

# Use Precompiled Headers to fix include order issues and speed up build
target_precompile_headers(BubiC-8801MA PRIVATE src/stdafx.h)

# Include directories
target_include_directories(BubiC-8801MA PRIVATE
    src
    src/vm
    src/sdl3
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

# Link SDL3
target_link_libraries(BubiC-8801MA PRIVATE SDL3::SDL3)

if(WIN32 AND MSVC)
    # Build as a GUI app on Windows while keeping int main(...) entry point.
    target_link_options(BubiC-8801MA PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# Bundle Dependencies
if(WIN32)
    # SDL3 runtime DLL path for packaging (preferred from workflow input).
    set(SDL3_RUNTIME_DLL "" CACHE FILEPATH "Path to SDL3 runtime DLL for packaging")
    if(SDL3_RUNTIME_DLL AND EXISTS "${SDL3_RUNTIME_DLL}")
        install(FILES "${SDL3_RUNTIME_DLL}" DESTINATION .) # DLLもインストールルートの直下に配置
    else()
        # Fallback: resolve DLL from imported SDL3 target.
        get_target_property(SDL3_LOCATION SDL3::SDL3 IMPORTED_LOCATION_RELEASE)
        if(NOT SDL3_LOCATION)
            get_target_property(SDL3_LOCATION SDL3::SDL3 IMPORTED_LOCATION)
        endif()
        if(SDL3_LOCATION)
            install(FILES ${SDL3_LOCATION} DESTINATION .) # DLLもインストールルートの直下に配置
        endif()
    endif()
elseif(APPLE)
    # バンドル内の Frameworks を検索パスに追加
    set_target_properties(BubiC-8801MA PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/assets/Info.plist
    )
endif()

if(APPLE)
    target_link_libraries(BubiC-8801MA PRIVATE "-framework CoreFoundation")
endif()

# Installation Rules
include(GNUInstallDirs)
install(TARGETS BubiC-8801MA
    # macOS は .app バンドルとしてルートに配置
    BUNDLE DESTINATION "$<$<BOOL:${APPLE}>:.>"
    # Windows は exe をインストールルートの直下に配置
    # Linux は標準の bin ディレクトリに配置
    RUNTIME DESTINATION "$<$<AND:$<BOOL:${WIN32}>,$<NOT:$<BOOL:${APPLE}>>>:.>$<$<NOT:$<BOOL:${WIN32}>>:$<IF:$<BOOL:${APPLE}>,.,${CMAKE_INSTALL_BINDIR}>>"
)

if(UNIX AND NOT APPLE)
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/linux/bubic-8801ma.desktop.in
        ${CMAKE_CURRENT_BINARY_DIR}/bubic-8801ma.desktop
        COPYONLY
    )

    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/bubic-8801ma.desktop
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications
    )

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_1024.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/1024x1024/apps
        RENAME bubic-8801ma.png
    )

    install(FILES
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon_1024.png
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pixmaps
        RENAME bubic-8801ma.png
    )
endif()

# CPack Configuration
set(CPACK_PACKAGE_NAME "BubiC-8801MA")
set(CPACK_PACKAGE_VENDOR "bubio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "BubiC-8801MA - PC-8801MA Emulator")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_CONTACT "bubio")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
elseif(UNIX AND NOT APPLE)
    set(CPACK_GENERATOR "DEB;RPM;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "bubio")
    set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libasound2, libx11-6")
endif()

include(CPack)

# Platform specific settings
if(APPLE)
    set_source_files_properties(${ICON_ICNS} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
    set_target_properties(BubiC-8801MA PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "BubiC-8801MA"
        MACOSX_BUNDLE_DISPLAY_NAME "BubiC-8801MA"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.bubio.BubiC-8801MA"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) 2026 Bubio"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    )
endif()

# Definition for SDL3
target_compile_definitions(BubiC-8801MA PRIVATE
    USE_SDL3
    _PC8801MA
)

if(WIN32)
    target_compile_definitions(BubiC-8801MA PRIVATE
        DISABLE_ZLIB
        WIN32_LEAN_AND_MEAN
    )
endif()

