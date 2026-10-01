# Configure shared compiler settings before any runtime or test targets.

# Optional compiler cache; generated PCH timestamps otherwise defeat cache reuse.
if(NOCTURNE_USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache: ${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        # Clang stamps each precompiled header with the time it was built, so a
        # rebuilt nocturne.h PCH never matches the last one and every file that
        # includes it misses the cache. Without the stamp the PCH is
        # reproducible. Configure ccache to permit PCH use as well.
        add_compile_options(
            "$<$<OR:$<COMPILE_LANG_AND_ID:C,Clang>,$<COMPILE_LANG_AND_ID:CXX,Clang>>:-Xclang=-fno-pch-timestamp>")
    else()
        message(STATUS "ccache: not found (install with apt/brew for faster rebuilds)")
    endif()
endif()

# Authenticity toggles. Each one defaults in
# shims/config/shim_config_authentic.h, behind an #ifndef, and every doc comment
# there tells the reader to override it on the configure line. Forward any that
# were set there as a real compiler -D so that instruction is true — without
# this they land in the cache as variables nothing reads, and the override is
# silently ignored.
#
# Deliberately global rather than per-target: shim_config.h reaches every TU
# through nocturne.h, so a toggle that only reached some targets would mean two
# halves of the build disagreeing about what the game does. Changing one is
# therefore a full rebuild, which is the honest cost of a switch that every
# translation unit can see.
# A vanilla build answers every toggle the way the shipped game did, so it has
# to populate the cache before the forwarding loop reads it.
if(NOCTURNE_VANILLA)
    include("${PROJECT_SOURCE_DIR}/src/cmake/vanilla.cmake")
endif()

# Forward explicit netplay trace switches alongside authenticity options.
get_cmake_property(_nocturne_cache_vars CACHE_VARIABLES)
set(_nocturne_authentic_defs "")
foreach(_var IN LISTS _nocturne_cache_vars)
    if(_var MATCHES "^NOCTURNE_(AUTHENTIC_[A-Z0-9_]+|EDITOR_BUILD|NETPLAY_(RNG|SIM)_TRACE)$")
        list(APPEND _nocturne_authentic_defs "${_var}=${${_var}}")
    endif()
endforeach()
if(_nocturne_authentic_defs)
    list(SORT _nocturne_authentic_defs)
    add_compile_definitions(${_nocturne_authentic_defs})
    string(REPLACE ";" "\n                  " _nocturne_authentic_pretty
           "${_nocturne_authentic_defs}")
    message(STATUS "authenticity overrides: ${_nocturne_authentic_pretty}")
endif()

