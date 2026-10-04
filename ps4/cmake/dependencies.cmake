# Target-only dependency provider. Desktop libraries never enter a PS4 link.
include(ExternalProject)
set(NOCTURNE_PS4_SDL_SOURCE_DIR "${NOCTURNE_DEPS_SOURCE_DIR}/sdl2")
add_subdirectory("${PROJECT_SOURCE_DIR}/ps4/sdl" "${CMAKE_BINARY_DIR}/sdl2")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static PS4 dependencies" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
add_subdirectory("${NOCTURNE_DEPS_SOURCE_DIR}/sdl2_ttf" "${CMAKE_BINARY_DIR}/sdl2_ttf")
add_library(SDL2_ttf::SDL2_ttf ALIAS SDL2_ttf)
set(SDL2_INCLUDE_DIRS "$<TARGET_PROPERTY:SDL2::SDL2,INTERFACE_INCLUDE_DIRECTORIES>")
set(SDL2_CFLAGS_OTHER -DSDL_BYTEORDER=1234)
set(SDL2_LIBRARIES SDL2::SDL2)
set(SDL2TTF_INCLUDE_DIRS "$<TARGET_PROPERTY:SDL2_ttf::SDL2_ttf,INTERFACE_INCLUDE_DIRECTORIES>")
set(SDL2TTF_LIBRARIES SDL2_ttf::SDL2_ttf)

set(_ff_root "${CMAKE_BINARY_DIR}/ffmpeg")
set(_ff_install "${_ff_root}/install")
set(_ps4_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
if(NOT _ps4_jobs MATCHES "^[1-9][0-9]*$")
    set(_ps4_jobs 8)
endif()
file(MAKE_DIRECTORY "${_ff_install}/include")
ExternalProject_Add(ffmpeg_ext
    SOURCE_DIR "${NOCTURNE_DEPS_SOURCE_DIR}/ffmpeg"
    DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
    PREFIX "${_ff_root}" INSTALL_DIR "${_ff_install}"
    CONFIGURE_COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/ps4/ffmpeg_configure.py"
        --source <SOURCE_DIR> --prefix <INSTALL_DIR> --sdk "${OO_PS4_TOOLCHAIN}"
        --sdk-include "${NOCTURNE_PS4_SDK_INCLUDE}"
        --cc "${CMAKE_C_COMPILER}" --ld "${CMAKE_LINKER}"
        --ar "${CMAKE_AR}" --ranlib "${CMAKE_RANLIB}"
    BUILD_COMMAND make -j${_ps4_jobs}
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${_ff_install}/lib/libavformat.a" "${_ff_install}/lib/libavcodec.a"
        "${_ff_install}/lib/libswscale.a" "${_ff_install}/lib/libswresample.a" "${_ff_install}/lib/libavutil.a")
ExternalProject_Add_StepDependencies(ffmpeg_ext configure
    "${PROJECT_SOURCE_DIR}/ps4/ffmpeg_configure.py"
    "${NOCTURNE_PS4_SDK_INCLUDE}/bits/alltypes.h")
set(FFMPEG_INCLUDE_DIRS "${_ff_install}/include")
set(FFMPEG_LIBRARIES "${_ff_install}/lib/libavformat.a" "${_ff_install}/lib/libavcodec.a"
    "${_ff_install}/lib/libswscale.a" "${_ff_install}/lib/libswresample.a" "${_ff_install}/lib/libavutil.a")
set(NOCTURNE_FFMPEG_DEP ffmpeg_ext)
