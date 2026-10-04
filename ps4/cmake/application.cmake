set_target_properties(nocturne PROPERTIES OUTPUT_NAME nocturne SUFFIX .elf)
target_sources(nocturne_shims PRIVATE "${PROJECT_SOURCE_DIR}/ps4/runtime/file_ops.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/filesystem.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/process.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/clock.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/sdk_abi.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/recursive_mutex.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/ime.cpp")
target_link_libraries(nocturne PRIVATE SceImeDialog SceNet SceSystemService SceSysmodule)
add_executable(nocturne_ps4_sdk_probe "${PROJECT_SOURCE_DIR}/ps4/tests/sdk_probe.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/sdk_abi.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/file_ops.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/filesystem.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/process.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/clock.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/recursive_mutex.cpp")
set_target_properties(nocturne_ps4_sdk_probe PROPERTIES SUFFIX .elf)
target_link_libraries(nocturne_ps4_sdk_probe PRIVATE SceSystemService SceSysmodule)
if(CMAKE_HOST_APPLE)
    set(_ps4_converter "${OO_PS4_TOOLCHAIN}/bin/macos/create-fself-macos")
else()
    set(_ps4_converter "${OO_PS4_TOOLCHAIN}/bin/linux/create-fself")
endif()
add_custom_command(OUTPUT "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/eboot.bin"
    COMMAND "${CMAKE_COMMAND}" -E env "OO_PS4_TOOLCHAIN=${OO_PS4_TOOLCHAIN}"
        "${_ps4_converter}" "-in=$<TARGET_FILE:nocturne>"
        "-out=$<TARGET_FILE_DIR:nocturne>/nocturne.oelf"
        --eboot "$<TARGET_FILE_DIR:nocturne>/eboot.bin" --paid 0x3800000000000011
    DEPENDS nocturne "${_ps4_converter}"
    VERBATIM COMMENT "Creating PS4 eboot.bin")
add_custom_target(nocturne_ps4_eboot ALL DEPENDS "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/eboot.bin")
add_custom_target(nocturne_ps4_resources ALL
    COMMAND "${CMAKE_COMMAND}" "-DSOURCE_ROOT=${PROJECT_SOURCE_DIR}"
        "-DDEPS=${NOCTURNE_DEPS_SOURCE_DIR}" "-DSDK=${OO_PS4_TOOLCHAIN}"
        "-DOUT=${CMAKE_BINARY_DIR}/resources/$<CONFIG>"
        -P "${PROJECT_SOURCE_DIR}/ps4/cmake/resources.cmake"
    DEPENDS nocturne_ps4_eboot VERBATIM)
