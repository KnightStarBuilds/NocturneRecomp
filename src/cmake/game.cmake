# Game targets and shared compile semantics.
# The root project owns options, paths, version, packaging and test registration.

# Third-party archives are verified and prepared by deps/prepare.py.
foreach(_dependency sdl2 sdl2_ttf ffmpeg liberation-fonts)
    if(NOT IS_DIRECTORY "${NOCTURNE_DEPS_SOURCE_DIR}/${_dependency}")
        message(FATAL_ERROR "Run ./build.sh to verify and prepare pinned dependencies first.")
    endif()
endforeach()
if(NOCTURNE_PS4)
    include("${PROJECT_SOURCE_DIR}/ps4/cmake/dependencies.cmake")
else()
    include("${CMAKE_CURRENT_LIST_DIR}/bundledlibs.cmake")
endif()

# More than 6,000 object paths exceed Darwin's command-line limit. Set this only
# after dependency subdirectories: Apple's ar cannot consume response files.
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)
set(NOCTURNE_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${NOCTURNE_GEN_DIR}")

# Version header, rewritten on every build so `git describe` cannot go stale.
# Only shims/core/version.cpp includes it, so a new commit recompiles one file.
# -----------------------------------------------------------------------------
set(NOCTURNE_VERSION_HEADER "${NOCTURNE_GEN_DIR}/nocturne_version.h")
if(DEFINED NOCTURNE_BUILD_TAG)
    set(_version_build_tag "${NOCTURNE_BUILD_TAG}")
else()
    set(_version_build_tag "${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
add_custom_target(nocturne_version ALL
    COMMAND "${CMAKE_COMMAND}"
            "-DVERSION=${PROJECT_VERSION}"
            "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
            "-DOUT=${NOCTURNE_VERSION_HEADER}"
            "-DTEMPLATE=${PROJECT_SOURCE_DIR}/src/cmake/nocturne_version.h.in"
            "-DBUILD_TAG=${_version_build_tag}"
            -P "${PROJECT_SOURCE_DIR}/src/cmake/write_version.cmake"
    BYPRODUCTS "${NOCTURNE_VERSION_HEADER}"
    COMMENT "Stamping version ${PROJECT_VERSION}"
    VERBATIM)
# The header must exist before the first compile, not merely before the first
# build — a fresh configure has no generated/ contents yet and the dependency
# below only orders the two, it does not create the file.
execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DVERSION=${PROJECT_VERSION}"
        "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
        "-DOUT=${NOCTURNE_VERSION_HEADER}"
        "-DTEMPLATE=${PROJECT_SOURCE_DIR}/src/cmake/nocturne_version.h.in"
        "-DBUILD_TAG=${_version_build_tag}"
        -P "${PROJECT_SOURCE_DIR}/src/cmake/write_version.cmake"
        COMMAND_ERROR_IS_FATAL ANY)
message(STATUS "version: ${PROJECT_VERSION} (${_version_build_tag})")

# Only the selected reconstruction units are tracked. Modules stay explicit so
# adding an unrelated source directory cannot silently change the executable.
set(NOCTURNE_MODULES cockpit core engine globals shape sound support wincore main checks)
set(_source_count 0)
foreach(_module IN LISTS NOCTURNE_MODULES)
    if(_module STREQUAL "main" OR _module STREQUAL "checks")
        set(_module_dir "${NOCTURNE_SOURCE_DIR}/${_module}")
    else()
        set(_module_dir "${NOCTURNE_GAME_DIR}/${_module}")
    endif()
    file(GLOB_RECURSE NOCTURNE_SOURCES_${_module} CONFIGURE_DEPENDS
        "${_module_dir}/*.cpp" "${_module_dir}/*.c")
    list(SORT NOCTURNE_SOURCES_${_module})
    list(LENGTH NOCTURNE_SOURCES_${_module} _module_count)
    if(_module_count EQUAL 0)
        message(FATAL_ERROR "No sources in required module ${_module_dir}")
    endif()
    math(EXPR _source_count "${_source_count} + ${_module_count}")
    message(STATUS "Nocturne ${_module}: ${_module_count} sources")
endforeach()

set(NOCTURNE_COMMON_WARNINGS
    -Wno-everything
    -Warray-bounds
    -Wformat
    -Wformat-overflow
    -Werror=format
    -Werror=format-overflow
    -Wno-format-security
    -Wno-format-extra-args
    -Wincompatible-pointer-types
    -Wint-conversion
    -Wreturn-type
    -Wtautological-compare
    # Ghidra splitting one reused register into two locals and leaving one
    # uninitialized (a decompiler artifact) — an error because the garbage it
    # feeds downstream surfaces far from its cause. Deliberately not the broader
    # -Wuninitialized, which also fires on in_ST0/in_ST1 x87 phantoms (a missing
    # CUSTOM_STORAGE signature in Ghidra, not a code defect).
    -Werror=sometimes-uninitialized
)

set(NOCTURNE_INCLUDE_FLAGS
    -I "${NOCTURNE_INCLUDE_DIR}"
    -I "${NOCTURNE_SHIMS_DIR}"
    -I "${NOCTURNE_SOURCE_DIR}/platform"
)

# -----------------------------------------------------------------------------
# Target: nocturne_check  (per-file -fsyntax-only)
# Produces a stamp per successfully checked file so incremental re-runs only
# touch changed files. This is the drop-in for the existing verification path.
# -----------------------------------------------------------------------------
if(NOCTURNE_BUILD_CHECK)
    set(_stamp_dir "${CMAKE_BINARY_DIR}/check_stamps")
    file(MAKE_DIRECTORY "${_stamp_dir}")
    set(_all_stamps "")
    # One shared dependency edge keeps header changes visible without attaching
    # the entire header tree to each of the thousands of syntax-check commands.
    file(GLOB_RECURSE _check_headers CONFIGURE_DEPENDS
        "${NOCTURNE_INCLUDE_DIR}/*.h" "${NOCTURNE_INCLUDE_DIR}/*.hpp"
        "${NOCTURNE_INCLUDE_DIR}/*.inc" "${NOCTURNE_INCLUDE_DIR}/*.inl"
        "${NOCTURNE_SHIMS_DIR}/*.h" "${NOCTURNE_SHIMS_DIR}/*.hpp"
        "${NOCTURNE_SOURCE_DIR}/platform/*.h"
        "${NOCTURNE_SOURCE_DIR}/main/*.h" "${NOCTURNE_SOURCE_DIR}/checks/*.h")
    list(SORT _check_headers)
    # Added/deleted headers must invalidate checks even when their mtimes are old.
    file(CONFIGURE OUTPUT "${_stamp_dir}/headers.txt" CONTENT "${_check_headers}\n" @ONLY)
    set(_header_stamp "${_stamp_dir}/headers.stamp")
    add_custom_command(OUTPUT "${_header_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_header_stamp}"
        DEPENDS ${_check_headers} "${_stamp_dir}/headers.txt"
        COMMENT "Refreshing syntax-check header dependencies"
        VERBATIM)

    separate_arguments(_check_compiler_flags NATIVE_COMMAND "${CMAKE_CXX_FLAGS}")
    foreach(_definition IN LISTS _nocturne_authentic_defs)
        list(APPEND _check_compiler_flags "-D${_definition}")
    endforeach()
    list(APPEND _check_compiler_flags "-DGAMEPAD_ONLY=$<BOOL:${GAMEPAD_ONLY}>")
    if(NOCTURNE_EXPERIMENTAL_RESOLUTIONS)
        list(APPEND _check_compiler_flags -DNOCTURNE_EXPERIMENTAL_RESOLUTIONS=1)
    else()
        list(APPEND _check_compiler_flags -DNOCTURNE_EXPERIMENTAL_RESOLUTIONS=0)
    endif()
    if(APPLE)
        list(APPEND _check_compiler_flags -isysroot "${CMAKE_OSX_SYSROOT}"
            "-mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET}")
        foreach(_architecture IN LISTS CMAKE_OSX_ARCHITECTURES)
            list(APPEND _check_compiler_flags -arch "${_architecture}")
        endforeach()
    endif()

    foreach(_module IN LISTS NOCTURNE_MODULES)
        set(_sources ${NOCTURNE_SOURCES_${_module}})
        foreach(_src IN LISTS _sources)
            # Relative paths keep game, main and check stamp names distinct.
            file(RELATIVE_PATH _rel "${NOCTURNE_SOURCE_DIR}" "${_src}")
            string(REPLACE "/" "_" _stamp_name "${_rel}")
            string(REPLACE "." "_" _stamp_name "${_stamp_name}")
            set(_stamp "${_stamp_dir}/${_stamp_name}.stamp")

            # Recovered .c files include C++ headers and require C++ semantics.
            add_custom_command(
                OUTPUT  "${_stamp}"
                COMMAND "${CMAKE_CXX_COMPILER}"
                        -fsyntax-only -std=c++23 -fwrapv -fno-strict-aliasing
                        ${_check_compiler_flags}
                        -x c++
                        ${NOCTURNE_COMMON_WARNINGS}
                        ${NOCTURNE_INCLUDE_FLAGS}
                        "${_src}"
                COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
                DEPENDS "${_src}" "${_header_stamp}"
                COMMENT "syntax ${_rel}"
                VERBATIM
            )
            list(APPEND _all_stamps "${_stamp}")
        endforeach()
    endforeach()

    add_custom_target(nocturne_check DEPENDS ${_all_stamps})
    message(STATUS "nocturne_check: ${_source_count} files")
endif()

# Sanitizer flags (applied to compile AND link for every exe-side target).
# Empty list when disabled — keeps the option a single toggle.
set(NOCTURNE_SANITIZE_FLAGS "")
if(NOCTURNE_ASAN)
    list(APPEND NOCTURNE_SANITIZE_FLAGS
        -fsanitize=address,undefined
        -fno-sanitize=function,shift,signed-integer-overflow,alignment
        -fwrapv
        -fno-omit-frame-pointer
        -fno-sanitize-recover=all)
    # -fno-sanitize=function: vtable dispatch type mismatches (normal C++ covariance)
    # -fno-sanitize=shift: fixed-point math left-shifts negative values pervasively
    # -fno-sanitize=signed-integer-overflow + -fwrapv: Watcom-compiled arithmetic
    #   assumes x86 two's-complement wrap (e.g. INT_MIN - N), pervasive in the binary
    # -fno-sanitize=alignment: structs are #pragma pack(1) to preserve the 32-bit
    #   Watcom layout, so 8-byte pointer fields land at 4-mod-8 offsets at 64-bit;
    #   misaligned scalar/pointer access is benign on x86-64 and pervasive/intentional
    message(STATUS "AddressSanitizer + UBSan enabled (function-type, shift, signed-overflow, and alignment checks disabled)")
endif()

# PCH: every decompiled TU opens with #include "nocturne.h", which pulls in
# the full type/global/prototype tree. Precompiling it once and REUSE_FROM
# across every module OBJECT library cuts full-rebuild cost dramatically.
set(_pch_source "")
set(_module_libs "")
foreach(_module IN LISTS NOCTURNE_MODULES)
    set(_sources ${NOCTURNE_SOURCES_${_module}})
    if(_sources)
        set_source_files_properties(${_sources} PROPERTIES LANGUAGE CXX)
        add_library(nocturne_${_module} OBJECT ${_sources})
        target_include_directories(nocturne_${_module} PRIVATE
            "${NOCTURNE_INCLUDE_DIR}" "${NOCTURNE_SHIMS_DIR}")
        target_compile_options(nocturne_${_module} PRIVATE
            ${NOCTURNE_COMMON_WARNINGS}
            ${NOCTURNE_SANITIZE_FLAGS})
        target_compile_features(nocturne_${_module} PRIVATE cxx_std_23)

        # Performance: the per-pixel software span rasterizers
        # (render{MMXPerspective,PerspectiveCorrect}Scanline{16,32}) are the
        # renderer's hot inner loops. At the Debug build's -O0 they dominate
        # framerate — e.g. the flashlight volumetric halo blends ~19 near
        # screen-filling additive quads/frame here. Build just these files at
        # -O2 so the rest of the tree stays -O0/debuggable. Output is
        # bit-identical: -fwrapv (global) preserves signed-overflow wrap, and
        # -fno-strict-aliasing keeps the decompiler's pervasive pointer
        # type-punning (framebuffer/z reads, packed-channel casts) valid that
        # -O2 would otherwise miscompile. -g and ASan still apply.
        foreach(_src IN LISTS _sources)
            if(_src MATCHES "Scanline(16|32)_FUN_")
                # SKIP_PRECOMPILE_HEADERS: the shared PCH is built at -O0, so a
                # file at -O2 can't REUSE_FROM it (__OPTIMIZE__ mismatch). These
                # few files just re-include nocturne.h directly.
                set_source_files_properties("${_src}" PROPERTIES
                    COMPILE_OPTIONS "-O2;-fno-strict-aliasing"
                    SKIP_PRECOMPILE_HEADERS ON)
            endif()
        endforeach()

        if(_pch_source STREQUAL "")
            target_precompile_headers(nocturne_${_module} PRIVATE
                "${NOCTURNE_INCLUDE_DIR}/nocturne.h")
            set(_pch_source nocturne_${_module})
        else()
            target_precompile_headers(nocturne_${_module}
                REUSE_FROM ${_pch_source})
        endif()

        list(APPEND _module_libs nocturne_${_module})
    endif()
endforeach()


# RECURSE: the shims are grouped into subdirectories by responsibility.
# Legacy-platform emulation, deleted wholesale once the engine no longer
# speaks these APIs: win32/ (kernel32, user32, wingdi, mmsystem, winsock,
# MCI, and the shim_init that wires them), directx/ (DirectDraw,
# DirectSound), watcom/ (Watcom C++ runtime and CRT bridge). Ours, and
# permanent: core/, gl/, renderer/, net/, game/, debug/, config/. A flat
# glob would drop every one of them from the build silently — nothing
# references a shim source by name, so the first symptom would be a link
# error far from the cause. Only shim_config.h stays at the root, because
# the generated nocturne.h includes it by bare name.
file(GLOB_RECURSE _shim_sources CONFIGURE_DEPENDS
    "${NOCTURNE_SHIMS_DIR}/*.cpp"
    "${NOCTURNE_SHIMS_DIR}/*.c"
)
# One socket backend, chosen by platform: winsock_posix.c over BSD sockets,
# winsock_win32.c over Winsock. Both define the same eighteen posix_* entry
# points that winsock.cpp calls, so compiling both is a duplicate-symbol
# link error rather than a preference.
if(WIN32)
    list(FILTER _shim_sources EXCLUDE REGEX "/win32/winsock_posix\\.c$")
else()
    list(FILTER _shim_sources EXCLUDE REGEX "/win32/winsock_win32\\.c$")
endif()
list(SORT _shim_sources)
set_source_files_properties(${_shim_sources} PROPERTIES LANGUAGE CXX)

# -------------------------------------------------------------------------
# Window icon.
#
# By default the icon is shims/core/window_icon_moon.h, a committed header
# of RGBA pixels. Nothing runs at build time for it.
#
# NOCTURNE_ICON_FROM_EXE swaps in the shipped game's own icon instead — the
# 32x32 4bpp moon in nocturne.exe's / nocedit.exe's PE resource directory.
# That one cannot be committed, since the game binaries stay out of the
# repository, so it has to be extracted from whichever exe the developer
# has locally. With the option on and no exe present, the committed header
# stays in use: no build fails for want of an icon.
#
# Only files that exist NOW can be listed as dependencies (Ninja rejects a
# dependency nothing produces), so dropping an exe in later needs a
# reconfigure. The stamp is what the build edge tracks; the header itself is
# a byproduct the generator rewrites only when its contents actually change,
# so an unchanged icon doesn't recompile the shims on every build.
# -------------------------------------------------------------------------
option(NOCTURNE_ICON_FROM_EXE
    "Use the shipped game exe's own icon instead of the committed one"
    OFF)
set(NOCTURNE_ICON_SIZE 128 CACHE STRING
    "Edge length in pixels of the icon extracted by NOCTURNE_ICON_FROM_EXE")

set(_icon_script "${NOCTURNE_SOURCE_DIR}/tools/extract_exe_icon.py")
set(_icon_header "${NOCTURNE_GEN_DIR}/nocturne_icon.h")
set(_icon_stamp  "${NOCTURNE_GEN_DIR}/nocturne_icon.stamp")
set(_icon_args "")
set(_icon_deps "${_icon_script}")
if(NOCTURNE_ICON_FROM_EXE)
    foreach(_icon_exe "${PROJECT_SOURCE_DIR}/data/original/nocturne.exe"
                      "${PROJECT_SOURCE_DIR}/data/original/nocedit.exe")
        if(EXISTS "${_icon_exe}")
            list(APPEND _icon_args --exe "${_icon_exe}")
            list(APPEND _icon_deps "${_icon_exe}")
        endif()
    endforeach()
endif()

if(_icon_args)
    list(GET _icon_args 1 _icon_first)
    message(STATUS "window icon: ${_icon_first} (${NOCTURNE_ICON_SIZE}px)")
    add_custom_command(
        OUTPUT "${_icon_stamp}"
        BYPRODUCTS "${_icon_header}"
        COMMAND "${Python3_EXECUTABLE}" "${_icon_script}"
                --out "${_icon_header}" --size ${NOCTURNE_ICON_SIZE} ${_icon_args}
        COMMAND "${CMAKE_COMMAND}" -E touch "${_icon_stamp}"
        DEPENDS ${_icon_deps}
        COMMENT "Extracting window icon"
        VERBATIM
    )
    add_custom_target(nocturne_icon DEPENDS "${_icon_stamp}")
else()
    if(NOCTURNE_ICON_FROM_EXE)
        message(WARNING "NOCTURNE_ICON_FROM_EXE is ON but neither "
                        "nocturne.exe nor nocedit.exe is present — "
                        "falling back to the committed icon")
    endif()
    message(STATUS "window icon: shims/core/window_icon_moon.h (committed)")
endif()

add_library(nocturne_shims OBJECT ${_shim_sources})
# NOCTURNE_GEN_DIR unconditionally: shims/core/version.cpp reads the
# generated version header whether or not the icon was extracted.
target_include_directories(nocturne_shims PRIVATE
    "${NOCTURNE_INCLUDE_DIR}" "${NOCTURNE_SHIMS_DIR}" "${NOCTURNE_GEN_DIR}"
    ${SDL2_INCLUDE_DIRS} ${SDL2TTF_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIRS})
add_dependencies(nocturne_shims nocturne_version)
# Tells shims/core/version.cpp the generated header is on its include path.
# Without it that file falls back to placeholder strings, which is what the
# standalone shim checks get when no build tree is available.
target_compile_definitions(nocturne_shims PRIVATE NOCTURNE_VERSION_GENERATED=1)
if(TARGET nocturne_icon)
    add_dependencies(nocturne_shims nocturne_icon)
    target_compile_definitions(nocturne_shims PRIVATE NOCTURNE_ICON_FROM_EXE=1)
endif()
target_compile_options(nocturne_shims PRIVATE
    ${NOCTURNE_COMMON_WARNINGS}
    ${NOCTURNE_SANITIZE_FLAGS}
    ${SDL2_CFLAGS_OTHER} ${SDL2TTF_CFLAGS_OTHER} ${FFMPEG_CFLAGS_OTHER})
target_compile_features(nocturne_shims PRIVATE cxx_std_23)
if(NOT NOCTURNE_GL_PRESENT)
    target_compile_definitions(nocturne_shims PRIVATE NOCTURNE_GL_PRESENT=0)
endif()

# FFmpeg is built by ExternalProject at build time; its headers must exist
# before the shim TU that includes them compiles.
if(NOCTURNE_FFMPEG_DEP)
    add_dependencies(nocturne_shims ${NOCTURNE_FFMPEG_DEP})
endif()
# SDL2's public headers (SDL.h, ...) are copied from the source tree into the
# build tree by SDL2's `sdl_headers_copy` custom target. The shim TUs that
# `#include <SDL.h>` must wait for that copy — otherwise the compile races
# the copy (the ordering is only incidentally correct on a full build, and
# building the shims target alone loses the race). Present on both the source
# -built lanes (bundledlibs.cmake); absent if SDL came from the system.
if(TARGET sdl_headers_copy)
    add_dependencies(nocturne_shims sdl_headers_copy)
endif()

add_executable(nocturne)
target_compile_features(nocturne PRIVATE cxx_std_23)
target_include_directories(nocturne PRIVATE
    "${NOCTURNE_INCLUDE_DIR}" "${NOCTURNE_SHIMS_DIR}"
    ${SDL2_INCLUDE_DIRS} ${SDL2TTF_INCLUDE_DIRS} ${FFMPEG_INCLUDE_DIRS})
target_compile_options(nocturne PRIVATE
    ${NOCTURNE_COMMON_WARNINGS}
    ${NOCTURNE_SANITIZE_FLAGS}
    ${SDL2_CFLAGS_OTHER} ${SDL2TTF_CFLAGS_OTHER} ${FFMPEG_CFLAGS_OTHER})
target_link_options(nocturne PRIVATE ${NOCTURNE_SANITIZE_FLAGS})
if(NOT WIN32 AND NOT APPLE AND NOT NOCTURNE_PS4)
    # SDL2 and SDL2_ttf are built here as shared objects, and the link
    # records their absolute paths inside the build tree. That is right for
    # running from build/, and useless once the binary is copied anywhere
    # else -- a release archive would carry a runpath naming a directory on
    # the machine that built it. $ORIGIN is appended rather than replacing
    # it, so the build tree keeps working and an archive that ships the .so
    # beside the binary finds them there.
    target_link_options(nocturne PRIVATE "-Wl,-rpath,$ORIGIN")
endif()

foreach(_lib IN LISTS _module_libs)
    target_link_libraries(nocturne PRIVATE ${_lib})
endforeach()
target_link_libraries(nocturne PRIVATE nocturne_shims
    ${SDL2_LIBRARIES} ${SDL2TTF_LIBRARIES} ${FFMPEG_LIBRARIES})
if(WIN32)
    # ws2_32 backs winsock_win32.c. The rest are what the kernel32 shim
    # reaches for once its POSIX arms give way to the Win32 ones: winmm for
    # the multimedia timer, version and imagehlp for the module queries.
    target_link_libraries(nocturne PRIVATE ws2_32 winmm version)

    # SDL2 and SDL2_ttf are built shared, and a PE resolves its imports from
    # the directory the exe sits in. Put them there so the build output is
    # something that runs when copied to a Windows machine, rather than
    # something that needs a DLL hunt first. FFmpeg is static, so it needs
    # nothing staged.
    add_custom_command(TARGET nocturne POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE:SDL2_ttf::SDL2_ttf>"
                "$<TARGET_FILE_DIR:nocturne>"
        COMMENT "Staging SDL DLLs beside the game executable"
        VERBATIM)

    # The C++ runtime DLLs. Cross-compiling they come out of the Debian
    # sysroot, which no Windows machine has, so they have to travel with the
    # exe. Building natively under MSYS2 they are already installed and on
    # PATH inside that shell — but only inside it, so they are staged there
    # too and the output directory means the same thing on both lanes: a
    # folder that runs.
    if(CMAKE_CROSSCOMPILING)
        set(_rt_search_dirs
            "/usr/lib/gcc/${NOCTURNE_WIN_TRIPLE}/*-posix"
            "${NOCTURNE_WIN_SYSROOT}/lib")
    else()
        # Wherever this clang came from — /ucrt64/bin under MSYS2.
        get_filename_component(_rt_bindir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        set(_rt_search_dirs "${_rt_bindir}")
    endif()

    foreach(_rt_dll libstdc++-6.dll libwinpthread-1.dll libgcc_s_seh-1.dll)
        set(_rt_found "")
        foreach(_rt_dir IN LISTS _rt_search_dirs)
            if(NOT _rt_found)
                file(GLOB_RECURSE _rt_found "${_rt_dir}/${_rt_dll}")
            endif()
        endforeach()
        if(_rt_found)
            list(GET _rt_found 0 _rt_src)
            add_custom_command(TARGET nocturne POST_BUILD
                COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${_rt_src}" "$<TARGET_FILE_DIR:nocturne>"
                VERBATIM)
        else()
            message(STATUS "windows lane: ${_rt_dll} not found — copy it "
                           "beside the exe by hand if the build does not "
                           "start on a machine without the toolchain")
        endif()
    endforeach()
endif()


# Native platform services are owned here, outside the recovered engine modules.
target_sources(nocturne_shims PRIVATE
    "${NOCTURNE_SOURCE_DIR}/platform/runtime_paths.cpp"
    "${NOCTURNE_SOURCE_DIR}/platform/text_stream.cpp"
    "${NOCTURNE_SOURCE_DIR}/platform/smoke_probe.cpp")
target_include_directories(nocturne_shims PRIVATE "${NOCTURNE_SOURCE_DIR}/platform")
target_include_directories(nocturne_main PRIVATE "${NOCTURNE_SOURCE_DIR}/platform")
