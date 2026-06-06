cmake_minimum_required(VERSION 3.20)
project(LapesEye VERSION 0.5.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Flagi kompilatora ─────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
elseif(MSVC)
    add_compile_options(/W3 /wd4100 /wd4267 /utf-8)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2)
    endif()
    # Wyłącz zbędne Windows.h makra
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

# Qt6
find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core Gui Widgets Concurrent Network Sql PrintSupport
)
find_package(Qt6 QUIET COMPONENTS OpenGL OpenGLWidgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# ── Zależności (Linux: pkg-config, Windows: vcpkg) ───────────────────────────
if(WIN32)
    # Windows: biblioteki przez vcpkg (cmake/toolchain)
    find_package(exiv2 CONFIG REQUIRED)
    find_package(libraw CONFIG REQUIRED)
    set(EXIV2_LIBRARIES  exiv2lib)
    set(LIBRAW_LIBRARIES libraw::libraw)
    set(EXIV2_VERSION    "vcpkg")
    set(LIBRAW_VERSION   "vcpkg")
else()
    # Linux: pkg-config
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(EXIV2   REQUIRED exiv2)
    pkg_check_modules(LIBRAW  REQUIRED libraw)
endif()

# Nagłówki z Q_OBJECT MUSZĄ być w LEYE_SOURCES żeby AUTOMOC je znalazł.
# Nasze nagłówki są w include/LapesEye/ — osobnym katalogu od src/ —
# dlatego trzeba je jawnie wymienić obok plików .cpp.
set(LEYE_HEADERS
    include/LapesEye/core/FileScanner.h
    include/LapesEye/core/LapeIPC.h
    include/LapesEye/ui/MainWindow.h
    include/LapesEye/ui/FolderPanel.h
    include/LapesEye/ui/ThumbnailGrid.h
    include/LapesEye/ui/ThumbnailItem.h
    include/LapesEye/ui/ThumbnailCanvas.h
    include/LapesEye/ui/PreviewPanel.h
    include/LapesEye/ui/MetaPanel.h
    include/LapesEye/ui/FilterBar.h
    include/LapesEye/ui/CompareView.h
    include/LapesEye/ui/FullscreenViewer.h
    include/LapesEye/ui/HistogramWidget.h
    include/LapesEye/ui/ColorLabelEditor.h
    include/LapesEye/ui/AdvancedSearchDialog.h
    include/LapesEye/ui/ExportDialog.h
    include/LapesEye/ui/PrintDialog.h
    include/LapesEye/ui/BreadcrumbBar.h
    include/LapesEye/ui/RubberOverlay.h
    include/LapesEye/ui/TearOffTabBar.h
    include/LapesEye/ui/BatchRenameDialog.h
    include/LapesEye/ui/CollectionDialog.h
    include/LapesEye/ui/SettingsDialog.h
    include/LapesEye/workers/ThumbWorker.h
    include/LapesEye/workers/ScanWorker.h
)

set(LEYE_SOURCES
    src/main.cpp

    src/core/FileScanner.cpp
    src/core/ThumbCache.cpp
    src/core/MetaStore.cpp
    src/core/ColorManagement.cpp
    src/core/CameraProfiles.cpp
    src/core/SonyILCE7M3Profile.cpp
    src/core/Collection.cpp
    src/core/LapeIPC.cpp

    src/ui/MainWindow.cpp
    src/ui/FolderPanel.cpp
    src/ui/ThumbnailGrid.cpp
    src/ui/ThumbnailItem.cpp
    src/ui/ThumbnailCanvas.cpp
    src/ui/PreviewPanel.cpp
    src/ui/MetaPanel.cpp
    src/ui/FilterBar.cpp
    src/ui/CompareView.cpp
    src/ui/FullscreenViewer.cpp
    src/ui/HistogramWidget.cpp
    src/ui/ColorLabelEditor.cpp
    src/ui/AdvancedSearchDialog.cpp
    src/ui/ExportDialog.cpp
    src/ui/PrintDialog.cpp
    src/ui/BreadcrumbBar.cpp
    src/ui/RubberOverlay.cpp
    src/ui/TearOffTabBar.cpp
    src/ui/BatchRenameDialog.cpp
    src/ui/CollectionDialog.cpp
    src/ui/SettingsDialog.cpp

    src/workers/ThumbWorker.cpp
    src/workers/ScanWorker.cpp
)

# Windows: RC file dla ikony w EXE i pasku zadań, WIN32 = brak czarnego okna konsoli
if(WIN32)
    configure_file(
        ${CMAKE_SOURCE_DIR}/resources/lapes-eye.rc.in
        ${CMAKE_BINARY_DIR}/lapes-eye.rc
        @ONLY)
    add_executable(lapes-eye WIN32
        ${LEYE_SOURCES} ${LEYE_HEADERS}
        resources/resources.qrc
        ${CMAKE_BINARY_DIR}/lapes-eye.rc)
else()
    add_executable(lapes-eye ${LEYE_SOURCES} ${LEYE_HEADERS} resources/resources.qrc)
endif()

target_include_directories(lapes-eye PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${EXIV2_INCLUDE_DIRS}
    ${LIBRAW_INCLUDE_DIRS}
)

target_link_libraries(lapes-eye PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Concurrent
    Qt6::Network
    Qt6::Sql
    Qt6::PrintSupport
    ${EXIV2_LIBRARIES}
    ${LIBRAW_LIBRARIES}
)

if(TARGET Qt6::OpenGL AND TARGET Qt6::OpenGLWidgets)
    target_link_libraries(lapes-eye PRIVATE Qt6::OpenGL Qt6::OpenGLWidgets)
    target_compile_definitions(lapes-eye PRIVATE LEYE_HAS_GL=1)
    message(STATUS "  GPU:          OpenGL 4.5 DSA (QOpenGLWidget)")
else()
    target_compile_definitions(lapes-eye PRIVATE LEYE_HAS_GL=0)
    message(STATUS "  GPU:          niedostępny (fallback QPainter)")
endif()

if(NOT WIN32)
    target_link_options(lapes-eye PRIVATE -rdynamic)
endif()

# ─── Zarządzanie kolorem — profil ICC monitora ───────────────────────────────
if(UNIX AND NOT APPLE)
    # Linux/X11: odczyt _ICC_PROFILE przez XCB (wbudowany w Qt6)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(XCB xcb)
        if(XCB_FOUND)
            target_link_libraries(lapes-eye PRIVATE ${XCB_LIBRARIES})
            target_include_directories(lapes-eye PRIVATE ${XCB_INCLUDE_DIRS})
            message(STATUS "  ICC monitor:  XCB (Linux/X11)")
        else()
            message(STATUS "  ICC monitor:  XCB niedostępny — fallback sRGB")
        endif()
    endif()
elseif(WIN32)
    # Windows: GetICMProfileW z mscms.dll
    target_link_libraries(lapes-eye PRIVATE mscms)
    message(STATUS "  ICC monitor:  WinAPI ICM (Windows)")
endif()

target_link_directories(lapes-eye PRIVATE
    ${EXIV2_LIBRARY_DIRS}
    ${LIBRAW_LIBRARY_DIRS}
)

install(TARGETS lapes-eye DESTINATION bin)

# Instalacja ikon w standardowych lokalizacjach freedesktop.org
# KDE/GNOME szukają ikon w /usr/share/icons/hicolor/<size>/apps/
foreach(SIZE 16 22 24 32 48 64 128 256 512)
    install(FILES resources/icons/lapes-eye-${SIZE}.png
            DESTINATION share/icons/hicolor/${SIZE}x${SIZE}/apps
            RENAME lapes-eye.png)
endforeach()

message(STATUS "")
message(STATUS "=== Lape's Eye — build configuration ===")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "  Qt version:   ${Qt6_VERSION}")
message(STATUS "  exiv2:        ${EXIV2_VERSION}")
message(STATUS "  LibRaw:       ${LIBRAW_VERSION}")
message(STATUS "")

# Instalacja pliku .desktop (usuwa ostrzeżenie DBus portal)
install(FILES resources/lapes-eye.desktop
        DESTINATION share/applications)
