cmake_minimum_required(VERSION 3.20)
project(ReiPan-SimplifiedChinese VERSION 0.1.0 LANGUAGES CXX)

set(REIPAN_RELEASE_VERSION "${PROJECT_VERSION} beta")
set(REIPAN_PROJECT_AUTHOR "kasumi-ko (霞詩@bilibili)")
set(REIPAN_PROJECT_URL "https://github.com/kasumi-ko/reipan-simplifiedchinese")
configure_file(cmake/reipan_version.h.in generated/reipan_version.h @ONLY)

# PC port of Fatal Frame 3 / Project Zero 3 (SLES_538.25), reconstructed function by
# function. The platform layer (src/reipan, src/sdk) comes from MioPan, the Fatal Frame 2
# port; game code under src/ is FF3's own.

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(WIN32 AND NOT MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "-static")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The nightly is built in the Steam Runtime 3 SDK for its old glibc, but
    # with its gcc-14, whose libstdc++ is newer than the one sniper and older
    # distros carry.  Link the C++ runtime in.  glibc itself has to stay
    # dynamic: SDL dlopens the GPU drivers and windowing libraries through it.
    string(APPEND CMAKE_EXE_LINKER_FLAGS " -static-libstdc++ -static-libgcc")
endif()

set(REIPAN_ICON "${CMAKE_CURRENT_SOURCE_DIR}/extern/reipan.ico")
if(WIN32 AND EXISTS "${REIPAN_ICON}")
    set(APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_SOURCE_DIR}/extern/icon.rc")
endif()
if(WIN32)
    configure_file(cmake/reipan_version.rc.in generated/reipan_version.rc @ONLY)
    list(APPEND APP_ICON_RESOURCE_WINDOWS "${CMAKE_CURRENT_BINARY_DIR}/generated/reipan_version.rc")
endif()

set(BUILD_SHARED_LIBS OFF)
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/ReiPan)

include(sdl3)
include(spdlog)
include(cglm)
include(imgui)
include(implot)
include(rmlui)
include(shadercross)
include(ffmpeg)
include(save_zlib)

file(GLOB_RECURSE REIPAN_TRANSLATION_UNITS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cc"
)
file(GLOB_RECURSE REIPAN_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.inc"
)
set(REIPAN_SOURCES ${REIPAN_TRANSLATION_UNITS} ${REIPAN_HEADERS})
set_source_files_properties(${REIPAN_HEADERS} PROPERTIES HEADER_FILE_ONLY TRUE)

set(REIPAN_INCLUDE_DIRS
    src
    src/sdk
    src/main
)

# FF3 is C++ throughout (classes, singletons, exceptions, std::string) and the .c files
# were compiled as C++ on the PS2 as well, so every translation unit builds as C++.
set_source_files_properties(${REIPAN_TRANSLATION_UNITS} PROPERTIES LANGUAGE CXX)

add_executable(ReiPan ${REIPAN_SOURCES} ${APP_ICON_RESOURCE_WINDOWS})
set_target_properties(ReiPan PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
option(REIPAN_SLPS_25544_CHS "Build for the adapted SLPS_255.44 Chinese game data" ON)
if(REIPAN_SLPS_25544_CHS)
    target_compile_definitions(ReiPan PRIVATE REIPAN_SLPS_25544_CHS=1)
endif()
add_dependencies(ReiPan compile_shaders)
target_include_directories(ReiPan PRIVATE ${REIPAN_INCLUDE_DIRS})
target_link_libraries(ReiPan PRIVATE
    ZLIB::ZLIBSTATIC
    SDL3::SDL3
    spdlog::spdlog
    cglm
    imgui::imgui
    implot::implot
    RmlUi::RmlUi
)

# The MPEG-2 decoder behind sceMpegGetPicture(); see cmake/ffmpeg.cmake.  When
# Windows prebuilt tree is absent the decoder compiles out. Other hosts
# build FFmpeg from source.
if(REIPAN_HAVE_FFMPEG)
    target_compile_definitions(ReiPan PRIVATE REIPAN_HAVE_FFMPEG=1)
    target_include_directories(ReiPan PRIVATE "${FFMPEG_INCLUDE_DIR}")
    target_link_libraries(ReiPan PRIVATE avcodec avutil swscale)
    # The import libraries resolve against DLLs that must sit next to the exe.
    if(WIN32)
        file(GLOB REIPAN_FFMPEG_DLLS CONFIGURE_DEPENDS "${REIPAN_FFMPEG_ROOT}/bin/*.dll")
        add_custom_command(TARGET ReiPan POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${REIPAN_FFMPEG_DLLS}
                    "$<TARGET_FILE_DIR:ReiPan>"
            VERBATIM
        )
    endif()
endif()

add_custom_command(TARGET ReiPan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/resources"
            "$<TARGET_FILE_DIR:ReiPan>/resources"
)

# The RmlUi menus' documents, style sheets and images, deployed next to the
# executable on every build -- the resources copy above only runs when the
# executable relinks -- so a menu can be restyled without touching code.
file(GLOB_RECURSE REIPAN_RML_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/rml/*")
set(REIPAN_RML_DEPLOYED)
foreach(rml_file ${REIPAN_RML_FILES})
    file(RELATIVE_PATH rml_relative "${CMAKE_SOURCE_DIR}" "${rml_file}")
    set(rml_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${rml_relative}")
    get_filename_component(rml_output_dir "${rml_output}" DIRECTORY)
    add_custom_command(
        OUTPUT "${rml_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${rml_output_dir}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${rml_file}" "${rml_output}"
        DEPENDS "${rml_file}"
        VERBATIM
    )
    list(APPEND REIPAN_RML_DEPLOYED "${rml_output}")
endforeach()
add_custom_target(deploy_rml DEPENDS ${REIPAN_RML_DEPLOYED})
add_dependencies(ReiPan deploy_rml)

# The UI's typeface, downloaded by cmake/rmlui.cmake.
add_custom_command(TARGET ReiPan POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "$<TARGET_FILE_DIR:ReiPan>/resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${REIPAN_RMLUI_FONT_FILES}
            "$<TARGET_FILE_DIR:ReiPan>/resources/fonts"
)

set_property(SOURCE src/reipan/io/reipan_log.cpp APPEND PROPERTY
    INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated")
message(STATUS "${PROJECT_NAME} ${REIPAN_RELEASE_VERSION}")

# Force-include the EE base-type shim into every TU so u_char/u_int/u_long (64-bit) and
# NULL are ambient exactly as the PS2 SDK's ubiquitous includes made them.
set(REIPAN_PREFIX_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/src/sdk/pc_prefix.h")
if(MSVC)
    target_compile_options(ReiPan PRIVATE "/FI${REIPAN_PREFIX_HEADER}")
else()
    target_compile_options(ReiPan PRIVATE -include "${REIPAN_PREFIX_HEADER}")
endif()

# ---- aliasing ------------------------------------------------------------
# NOT OPTIONAL. Reconstructed code reads the game's file formats in place and walks DMA
# packets through u_int*; GCC/Clang exploit the strict-aliasing violation from -O2 up
# (MioPan's release-only crash in gra3dsgdDrawPresetDataObject). MSVC needs nothing.
if(NOT MSVC)
    target_compile_options(ReiPan PRIVATE -fno-strict-aliasing)
endif()

# ---- buffer-overflow checks in Debug --------------------------------------
# Reconstructed code keeps the game's own overruns; fortifying Debug makes them abort
# under a debugger. It needs optimisation, hence -Og (-DREIPAN_DEBUG_FORTIFY=OFF for -O0).
option(REIPAN_DEBUG_FORTIFY
       "Build the GCC/Clang Debug config at -Og with _FORTIFY_SOURCE=3" ON)
if(NOT MSVC AND REIPAN_DEBUG_FORTIFY)
    target_compile_options(ReiPan PRIVATE $<$<CONFIG:Debug>:-Og>)
    target_compile_definitions(ReiPan PRIVATE $<$<CONFIG:Debug>:_FORTIFY_SOURCE=3>)
endif()

# ---- host-compiler quirks ----------------------------------------------
if(MSVC)
    target_compile_options(ReiPan PRIVATE
        /permissive
        /W1
        /wd4996
        /D_CRT_SECURE_NO_WARNINGS
        /Zc:preprocessor
    )
else()
    target_compile_options(ReiPan PRIVATE -w -fpermissive)
endif()
