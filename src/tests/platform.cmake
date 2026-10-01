add_executable(native_platform_tests
    "${CMAKE_CURRENT_LIST_DIR}/platform_tests.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/runtime_paths.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/text_stream.cpp"
    "${NOCTURNE_SHIMS_DIR}/watcom/path.cpp"
    "${NOCTURNE_SHIMS_DIR}/core/file_search.cpp")
target_include_directories(native_platform_tests PRIVATE
    "${PROJECT_SOURCE_DIR}/src/platform"
    "${NOCTURNE_SHIMS_DIR}")
target_compile_features(native_platform_tests PRIVATE cxx_std_23)
add_test(NAME native_platform COMMAND native_platform_tests)

function(native_shim_test name source)
    add_executable(native_${name} "${NOCTURNE_TESTS_DIR}/${source}" ${ARGN})
    target_include_directories(native_${name} PRIVATE
        "${NOCTURNE_TESTS_DIR}" "${NOCTURNE_SHIMS_DIR}"
        "${NOCTURNE_INCLUDE_DIR}" "${PROJECT_SOURCE_DIR}/src/platform" ${SDL2_INCLUDE_DIRS})
    target_compile_options(native_${name} PRIVATE -Wno-everything)
    target_compile_features(native_${name} PRIVATE cxx_std_23)
    add_test(NAME platform/${name} COMMAND native_${name})
endfunction()
set(_native_path "${NOCTURNE_SHIMS_DIR}/watcom/path.cpp")
set(_native_search "${NOCTURNE_SHIMS_DIR}/core/file_search.cpp")
native_shim_test(ascii_case shims/core/test_ascii_case.cpp)
native_shim_test(walk_turn shims/game/test_walk_turn.cpp
    "${NOCTURNE_SHIMS_DIR}/game/walk_turn.cpp")
native_shim_test(path shims/watcom/test_path.cpp "${_native_path}")
native_shim_test(file_search shims/core/test_file_search.cpp "${_native_path}" "${_native_search}")
native_shim_test(watcom shims/watcom/test_watcom.cpp
    "${NOCTURNE_SHIMS_DIR}/watcom/watcom.cpp" "${_native_path}" "${_native_search}")
native_shim_test(kernel32 shims/win32/test_kernel32.cpp
    "${NOCTURNE_SHIMS_DIR}/win32/kernel32.cpp"
    "${NOCTURNE_GAME_DIR}/globals/globals_610000.cpp"
    "${NOCTURNE_TESTS_DIR}/shims/win32/builtin_dll_support.cpp"
    "${PROJECT_SOURCE_DIR}/src/platform/runtime_paths.cpp"
    "${_native_path}" "${_native_search}")


# SDL's dummy driver exercises the real cursor/message bridge without a display.
native_shim_test(user32 shims/win32/test_user32.cpp
    "${NOCTURNE_SHIMS_DIR}/win32/user32.cpp"
    "${NOCTURNE_GAME_DIR}/globals/globals_610000.cpp")
target_link_libraries(native_user32 PRIVATE SDL2::SDL2)
target_compile_definitions(native_user32 PRIVATE NOCTURNE_GL_PRESENT=0 SDL_MAIN_HANDLED)

# Compile the actual controller and recovered binding pipeline. The fixture
# supplies only device sampling/focus and unrelated engine services.
set(_game_controls "${NOCTURNE_GAME_DIR}/core/game.cpp")
add_executable(native_gamepad_controls
    "${CMAKE_CURRENT_LIST_DIR}/gamepad_controls.cpp"
    "${NOCTURNE_SHIMS_DIR}/game/gamepad.cpp"
    "${_game_controls}/CGame_processJoystickControls_FUN_004dc230.keep.cpp"
    "${_game_controls}/CGame_processKeyboardControls_FUN_004dc3e0.keep.cpp"
    "${_game_controls}/CGame_resetKeyState_FUN_004dbe60.keep.cpp")
target_include_directories(native_gamepad_controls PRIVATE
    "${NOCTURNE_SHIMS_DIR}" "${NOCTURNE_INCLUDE_DIR}")
target_link_libraries(native_gamepad_controls PRIVATE SDL2::SDL2)
target_compile_features(native_gamepad_controls PRIVATE cxx_std_23)
target_compile_definitions(native_gamepad_controls PRIVATE NOCTURNE_DISABLE_DLOG SDL_MAIN_HANDLED)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Recovered constants contain narrowed integer bit patterns and char* APIs.
    target_compile_options(native_gamepad_controls PRIVATE
        -Wno-c++11-narrowing -Wno-writable-strings -Wno-constant-conversion)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(native_gamepad_controls PRIVATE -Wno-narrowing -Wno-write-strings)
endif()
add_test(NAME platform/gamepad_controls COMMAND native_gamepad_controls)
