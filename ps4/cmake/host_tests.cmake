# Console policies are testable on the host; these do not execute PS4 binaries.
if(UNIX)
    # These fixtures exercise POSIX paths and the macOS/Linux cross-build scripts.
    add_executable(native_ps4_runtime
        "${PROJECT_SOURCE_DIR}/ps4/tests/runtime_tests.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/runtime_paths.cpp"
        "${PROJECT_SOURCE_DIR}/src/platform/text_stream.cpp"
        "${PROJECT_SOURCE_DIR}/src/shims/game/resolution.cpp"
        "${PROJECT_SOURCE_DIR}/src/shims/renderer/builtin_dll.cpp")
    target_include_directories(native_ps4_runtime PRIVATE "${PROJECT_SOURCE_DIR}/ps4/runtime"
        "${PROJECT_SOURCE_DIR}/src/platform" "${PROJECT_SOURCE_DIR}/src/shims")
    target_compile_definitions(native_ps4_runtime PRIVATE NOCTURNE_PS4=1 NOCTURNE_GL_PRESENT=0)
    set_property(TARGET native_ps4_runtime PROPERTY NOCTURNE_GAMEPAD_ONLY ON)
    add_test(NAME ps4/runtime_policy COMMAND native_ps4_runtime)
    add_executable(native_ps4_filesystem
        "${PROJECT_SOURCE_DIR}/ps4/tests/filesystem_tests.cpp"
        "${PROJECT_SOURCE_DIR}/ps4/runtime/filesystem.cpp")
    target_include_directories(native_ps4_filesystem PRIVATE "${PROJECT_SOURCE_DIR}/ps4/runtime")
    add_test(NAME ps4/filesystem COMMAND native_ps4_filesystem)
    add_executable(native_ps4_logging
        "${PROJECT_SOURCE_DIR}/ps4/tests/logging_tests.cpp"
        "${PROJECT_SOURCE_DIR}/ps4/runtime/file_ops.cpp")
    target_include_directories(native_ps4_logging PRIVATE "${PROJECT_SOURCE_DIR}/ps4/runtime")
    add_test(NAME ps4/logging COMMAND native_ps4_logging)
    add_test(NAME ps4/packaging COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/ps4/tests/test_packaging.py")
    add_test(NAME ps4/build_scripts COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/ps4/tests/test_build.py")
    add_test(NAME ps4/validation COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/ps4/tests/test_validation.py")
endif()
add_test(NAME ps4/sdk_abi COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/ps4/tests/test_sdk_abi.py")
add_executable(native_ps4_presentation "${PROJECT_SOURCE_DIR}/ps4/tests/presentation_tests.c")
add_test(NAME ps4/presentation COMMAND native_ps4_presentation)
add_executable(native_ps4_clock "${PROJECT_SOURCE_DIR}/ps4/tests/clock_tests.cpp"
    "${PROJECT_SOURCE_DIR}/ps4/runtime/clock.cpp")
add_test(NAME ps4/clock COMMAND native_ps4_clock)

# Reuse the real controller pipeline with console defaults and host SDL doubles.
add_executable(native_ps4_gamepad_controls
    "${PROJECT_SOURCE_DIR}/src/tests/gamepad_controls.cpp"
    "${PROJECT_SOURCE_DIR}/src/shims/game/gamepad.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/core/game.cpp/CGame_processJoystickControls_FUN_004dc230.keep.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/core/game.cpp/CGame_processKeyboardControls_FUN_004dc3e0.keep.cpp"
    "${PROJECT_SOURCE_DIR}/src/game/core/game.cpp/CGame_resetKeyState_FUN_004dbe60.keep.cpp")
target_include_directories(native_ps4_gamepad_controls PRIVATE
    "${PROJECT_SOURCE_DIR}/src/include" "${PROJECT_SOURCE_DIR}/src/shims")
target_link_libraries(native_ps4_gamepad_controls PRIVATE SDL2::SDL2)
target_compile_features(native_ps4_gamepad_controls PRIVATE cxx_std_23)
target_compile_definitions(native_ps4_gamepad_controls PRIVATE
    NOCTURNE_PS4=1 NOCTURNE_DISABLE_DLOG SDL_MAIN_HANDLED)
set_property(TARGET native_ps4_gamepad_controls PROPERTY NOCTURNE_GAMEPAD_ONLY ON)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(native_ps4_gamepad_controls PRIVATE
        -Wno-c++11-narrowing -Wno-writable-strings -Wno-constant-conversion)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(native_ps4_gamepad_controls PRIVATE -Wno-narrowing -Wno-write-strings)
endif()
add_test(NAME ps4/gamepad_controls COMMAND native_ps4_gamepad_controls)
include("${PROJECT_SOURCE_DIR}/ps4/cmake/backend_tests.cmake")
