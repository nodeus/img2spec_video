cmake_minimum_required(VERSION 3.7)
project(Img2Spec)

set(CMAKE_CXX_STANDARD 14)

find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})
include_directories("src/imgui" "src/parson" "src/stb" "src/platform")

set(SOURCES
        src/main.cpp

        src/imgui/imconfig.h
        src/imgui/imgui.cpp
        src/imgui/imgui.h
        src/imgui/imgui_draw.cpp
        src/imgui/imgui_impl_sdl.cpp
        src/imgui/imgui_impl_sdl.h
        src/imgui/imgui_internal.h
        src/imgui/imgui_user.inl

        src/parson/parson.c
        src/parson/parson.h

        src/device.h
        src/modifier.h
        src/imgui_utils.h
        src/videopipeline.h
        src/keyframemanager.h
        src/exportmanager.h

        src/zxspectrumdevice.h
        src/zx3x64device.h
        src/zxhalftiledevice.h
        src/c64hiresdevice.h
        src/c64multicolordevice.h

        src/scaleposmodifier.h
        src/rgbmodifier.h
        src/yiqmodifier.h
        src/hsvmodifier.h
        src/noisemodifier.h
        src/blurmodifier.h
        src/edgemodifier.h
        src/quantizemodifier.h
        src/minmaxmodifier.h
        src/ordereddithermodifier.h
        src/errordiffusiondithermodifier.h
        src/monodithermodifier.h
        src/dither_bridge.h
        src/contrastmodifier.h
        src/superblackmodifier.h
        src/curvemodifier.h

        src/platform/common.h

        )

# Vendored libdither (C99/C11, MIT + kdtree/uthash attributions, see src/libdither/VENDOR.txt)
set(LIBDITHER_SOURCES
        src/libdither/libdither.c
        src/libdither/ditherimage.c
        src/libdither/random.c
        src/libdither/gamma.c
        src/libdither/queue.c
        src/libdither/dither_dbs.c
        src/libdither/dither_dotdiff.c
        src/libdither/dither_errordiff.c
        src/libdither/dither_kallebach.c
        src/libdither/dither_ordered.c
        src/libdither/dither_riemersma.c
        src/libdither/dither_threshold.c
        src/libdither/dither_varerrdiff.c
        src/libdither/dither_pattern.c
        src/libdither/dither_dotlippens.c
        src/libdither/dither_grid.c
        src/libdither/color_bytepalette.c
        src/libdither/color_floatcolor.c
        src/libdither/color_models.c
        src/libdither/color_quant_mediancut.c
        src/libdither/color_cachedpalette.c
        src/libdither/color_colorimage.c
        src/libdither/color_floatpalette.c
        src/libdither/color_bytecolor.c
        src/libdither/color_quant_wu.c
        src/libdither/color_quant_kdtree.c
        src/libdither/kdtree/kdtree.c
        src/libdither/tetrapal/tetrapal.c
        )
# MSVC compiles .c as legacy C by default and GCC 14+ defaults to C23;
# libdither needs C11/C17 (for-init decls, and tetrapal's bool typedef).
# (C_STANDARD is set on the target after add_executable, see below.)
set(SOURCES ${SOURCES} ${LIBDITHER_SOURCES})

if (WIN32)
    set(SOURCES ${SOURCES}
            src/platform/win32.cpp
            src/platform/win32.h)
else ()
    set(SOURCES ${SOURCES}
            src/platform/nonwin32.cpp
            src/platform/nonwin32.h
            src/platform/noc_file_dialog.h)
    if (APPLE)
        add_definitions(-DNOC_FILE_DIALOG_OSX)
        find_library(APPKIT AppKit)
        set_source_files_properties(src/platform/nonwin32.cpp
                PROPERTIES LANGUAGE C COMPILE_FLAGS "-x objective-c")
    else ()
        add_definitions(-DNOC_FILE_DIALOG_GTK)
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
        include_directories(${GTK3_INCLUDE_DIRS})
        link_directories(${GTK3_LIBRARY_DIRS})
        add_definitions(${GTK3_CFLAGS_OTHER})
    endif ()
endif ()

add_definitions(-DIMGUI_INCLUDE_IMGUI_USER_INL)

add_executable(img2spec_video WIN32 ${SOURCES})
set_target_properties(img2spec_video PROPERTIES OUTPUT_NAME "img2spec_video")
# See LIBDITHER_SOURCES above: vendored C code requires C11.
set_target_properties(img2spec_video PROPERTIES C_STANDARD 11)
if (WIN32)
    target_link_libraries(img2spec_video -Wl,--whole-archive ${SDL2_MAIN_LIBRARY} -Wl,--no-whole-archive)
endif ()
target_link_libraries(img2spec_video ${SDL2_LIBRARIES} ${OPENGL_LIBRARIES})

if (WIN32)
    target_link_libraries(img2spec_video winmm imm32 version)
endif ()

if (GTK3_FOUND)
    target_link_libraries(img2spec_video ${GTK3_LIBRARIES})
endif ()

if (APPKIT)
    target_link_libraries(img2spec_video ${APPKIT})
endif ()
