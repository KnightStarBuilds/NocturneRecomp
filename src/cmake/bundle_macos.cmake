# Copy only our runtime libraries; all other linkage must resolve to macOS.
file(MAKE_DIRECTORY "${APP}/Contents/Frameworks" "${APP}/Contents/Resources/fonts"
                    "${APP}/Contents/Resources/licenses")
foreach(_library "${SDL}" "${TTF}")
    get_filename_component(_name "${_library}" NAME)
    file(COPY_FILE "${_library}" "${APP}/Contents/Frameworks/${_name}" ONLY_IF_DIFFERENT)
    execute_process(COMMAND /usr/bin/install_name_tool -id "@rpath/${_name}"
        "${APP}/Contents/Frameworks/${_name}" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND /usr/bin/otool -l "${APP}/Contents/Frameworks/${_name}"
        OUTPUT_VARIABLE _commands COMMAND_ERROR_IS_FATAL ANY)
    string(REGEX MATCHALL "path [^\n]+ \\(offset [0-9]+\\)" _rpaths "${_commands}")
    foreach(_rpath IN LISTS _rpaths)
        string(REGEX REPLACE "^path (.*) \\(offset [0-9]+\\)$" "\\1" _rpath "${_rpath}")
        execute_process(COMMAND /usr/bin/install_name_tool -delete_rpath "${_rpath}"
            "${APP}/Contents/Frameworks/${_name}" COMMAND_ERROR_IS_FATAL ANY)
    endforeach()
    execute_process(COMMAND /usr/bin/install_name_tool -add_rpath "@loader_path"
        "${APP}/Contents/Frameworks/${_name}" COMMAND_ERROR_IS_FATAL ANY)
endforeach()
get_filename_component(_sdl_name "${SDL}" NAME)
get_filename_component(_ttf_name "${TTF}" NAME)
# CMake/SDL versions differ in whether build-tree dylib IDs are absolute or @rpath.
foreach(_binary "${EXECUTABLE}" "${APP}/Contents/Frameworks/${_ttf_name}")
    execute_process(COMMAND /usr/bin/otool -L "${_binary}" OUTPUT_VARIABLE _linked
                    COMMAND_ERROR_IS_FATAL ANY)
    foreach(_library "${SDL}" "${TTF}")
        get_filename_component(_name "${_library}" NAME)
        string(REGEX MATCH "[^\n\t ]*${_name}" _old "${_linked}")
        if(_old AND NOT _old STREQUAL "@rpath/${_name}")
            execute_process(COMMAND /usr/bin/install_name_tool -change "${_old}"
                "@rpath/${_name}" "${_binary}" COMMAND_ERROR_IS_FATAL ANY)
        endif()
    endforeach()
endforeach()
file(COPY_FILE "${DEPS}/liberation-fonts/LiberationMono-Regular.ttf"
               "${APP}/Contents/Resources/fonts/Fallback.ttf" ONLY_IF_DIFFERENT)
foreach(_entry
    "sdl2/LICENSE.txt" "sdl2_ttf/LICENSE.txt"
    "sdl2_ttf/external/freetype/docs/FTL.TXT" "sdl2_ttf/external/harfbuzz/COPYING"
    "ffmpeg/COPYING.LGPLv2.1" "ffmpeg/LICENSE.md" "liberation-fonts/LICENSE")
    string(REPLACE "/" "-" _license_name "${_entry}")
    file(COPY_FILE "${DEPS}/${_entry}"
         "${APP}/Contents/Resources/licenses/${_license_name}" ONLY_IF_DIFFERENT)
endforeach()
file(COPY_FILE "${SOURCE_ROOT}/docs/LICENSE.md" "${APP}/Contents/Resources/licenses/LICENSE.md"
     ONLY_IF_DIFFERENT)
file(COPY_FILE "${SOURCE_ROOT}/docs/NocturneDecomp-LICENSE" "${APP}/Contents/Resources/licenses/NocturneDecomp-LICENSE"
     ONLY_IF_DIFFERENT)
file(COPY_FILE "${SOURCE_ROOT}/src/resources/gamecontrollerdb.txt" "${APP}/Contents/Resources/gamecontrollerdb.txt"
     ONLY_IF_DIFFERENT)
file(COPY_FILE "${SOURCE_ROOT}/src/resources/gamecontrollerdb.license.txt"
     "${APP}/Contents/Resources/licenses/gamecontrollerdb-license.txt" ONLY_IF_DIFFERENT)
# Finder metadata can follow copied resources and prevents codesign from sealing
# the bundle. Strip it from the generated app, leaving source files untouched.
execute_process(COMMAND /usr/bin/xattr -cr "${APP}"
                COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND /usr/bin/codesign --force --deep --sign - "${APP}"
                COMMAND_ERROR_IS_FATAL ANY)
