cmake_minimum_required(VERSION 3.24)
project(NocturneRecompKnightStar VERSION 0.1.1 LANGUAGES C CXX)

if(NOT CMAKE_GENERATOR STREQUAL "Ninja Multi-Config")
    message(FATAL_ERROR "Use Ninja Multi-Config (or ./build.sh).")
endif()
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Supported configurations" FORCE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
# Recovered arithmetic and pointer casts require these semantics in both configurations.
add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fwrapv>
                    $<$<COMPILE_LANGUAGE:CXX>:-fno-strict-aliasing>)

set(NOCTURNE_SOURCE_DIR "${PROJECT_SOURCE_DIR}/src")
set(NOCTURNE_GAME_DIR "${NOCTURNE_SOURCE_DIR}/game")
set(NOCTURNE_INCLUDE_DIR "${NOCTURNE_SOURCE_DIR}/include")
set(NOCTURNE_SHIMS_DIR "${NOCTURNE_SOURCE_DIR}/shims")
set(NOCTURNE_TESTS_DIR "${NOCTURNE_SOURCE_DIR}/tests")
set(NOCTURNE_DEPS_SOURCE_DIR "${CMAKE_BINARY_DIR}/dependency-sources")
option(NOCTURNE_BUILD_CHECK "Build optional per-file syntax checks" OFF)
option(NOCTURNE_USE_CCACHE "Use ccache if installed" OFF)
option(NOCTURNE_ASAN "Enable AddressSanitizer and supported UBSan checks" OFF)
option(NOCTURNE_GL_PRESENT "Present through OpenGL (OFF = legacy SDL_Renderer)" ON)
option(NOCTURNE_VANILLA "Set every authenticity toggle to the shipped behaviour" OFF)
option(NOCTURNE_EXPERIMENTAL_RESOLUTIONS "Enable unvalidated internal resolutions on macOS" OFF)
add_compile_definitions(NOCTURNE_EXPERIMENTAL_RESOLUTIONS=$<BOOL:${NOCTURNE_EXPERIMENTAL_RESOLUTIONS}>)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
include(src/cmake/options.cmake)
add_subdirectory(src)
# The shim tests compile SDL consumers directly; expose the same public headers.
set(SDL2_INCLUDE_DIRS "$<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>")

if(APPLE)
    # Packed recovered globals contain unaligned pointers; traditional dyld
    # relocation opcodes support them, chained pointer fixups do not.
    target_link_options(nocturne PRIVATE "-Wl,-no_fixup_chains")
    set_target_properties(nocturne PROPERTIES
        MACOSX_BUNDLE TRUE OUTPUT_NAME NocturneRecompKnightStar
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/src/cmake/Info.plist.in"
        BUILD_RPATH "@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path/../Frameworks")
    add_custom_target(nocturne_bundle ALL
        COMMAND "${CMAKE_COMMAND}"
            "-DAPP=$<TARGET_BUNDLE_DIR:nocturne>"
            "-DEXECUTABLE=$<TARGET_FILE:nocturne>"
            "-DSDL=$<TARGET_FILE:SDL2::SDL2>"
            "-DTTF=$<TARGET_FILE:SDL2_ttf::SDL2_ttf>"
            "-DDEPS=${NOCTURNE_DEPS_SOURCE_DIR}"
            "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
            -P "${PROJECT_SOURCE_DIR}/src/cmake/bundle_macos.cmake"
        DEPENDS nocturne VERBATIM)
else()
    set_target_properties(nocturne PROPERTIES OUTPUT_NAME nocturne_recomp_knightStar)
endif()

enable_testing()
include(src/tests/platform.cmake)
include(src/tests/renderer.cmake)
add_test(NAME native_scripts COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/src/tests/scripts.py")
if(APPLE)
    add_test(NAME native_bundle COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/src/tests/bundle_check.py" "$<TARGET_BUNDLE_DIR:nocturne>")
endif()
# Keep a convenient target for all C++ test binaries; normal builds include them too.
get_property(_all_targets DIRECTORY PROPERTY BUILDSYSTEM_TARGETS)
list(FILTER _all_targets INCLUDE REGEX "^native_")
add_custom_target(nocturne_tests DEPENDS ${_all_targets})
