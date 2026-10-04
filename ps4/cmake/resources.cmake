file(MAKE_DIRECTORY "${OUT}/fonts" "${OUT}/licenses" "${OUT}/sce_sys/about" "${OUT}/sce_module")
file(COPY_FILE "${DEPS}/liberation-fonts/LiberationMono-Regular.ttf" "${OUT}/fonts/Fallback.ttf" ONLY_IF_DIFFERENT)
file(COPY_FILE "${SOURCE_ROOT}/src/resources/gamecontrollerdb.txt" "${OUT}/gamecontrollerdb.txt" ONLY_IF_DIFFERENT)
foreach(_entry "sdl2/LICENSE.txt" "sdl2_ttf/LICENSE.txt"
    "sdl2_ttf/external/freetype/docs/FTL.TXT" "ffmpeg/COPYING.LGPLv2.1"
    "ffmpeg/LICENSE.md" "liberation-fonts/LICENSE")
    string(REPLACE "/" "-" _name "${_entry}")
    file(COPY_FILE "${DEPS}/${_entry}" "${OUT}/licenses/${_name}" ONLY_IF_DIFFERENT)
endforeach()
foreach(_entry "docs/LICENSE.md" "docs/NocturneDecomp-LICENSE"
    "src/resources/gamecontrollerdb.license.txt")
    get_filename_component(_name "${_entry}" NAME)
    file(COPY_FILE "${SOURCE_ROOT}/${_entry}" "${OUT}/licenses/${_name}" ONLY_IF_DIFFERENT)
endforeach()
file(COPY_FILE "${SDK}/LICENSE" "${OUT}/licenses/OpenOrbis-LICENSE" ONLY_IF_DIFFERENT)
file(COPY "${SOURCE_ROOT}/ps4/licenses/" DESTINATION "${OUT}/licenses")
# Firmware loads these OpenOrbis stubs before main, even with static musl.
# Keep the pinned SDK's module bytes and package paths used by its samples.
foreach(_module libc.prx libSceFios2.prx)
    file(COPY_FILE "${SDK}/samples/hello_world/sce_module/${_module}"
        "${OUT}/sce_module/${_module}" ONLY_IF_DIFFERENT)
endforeach()
file(COPY_FILE "${SDK}/samples/hello_world/sce_sys/about/right.sprx" "${OUT}/sce_sys/about/right.sprx" ONLY_IF_DIFFERENT)
