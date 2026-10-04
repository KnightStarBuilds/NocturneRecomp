# Compile production PS4 backends with narrow device doubles, never host SDL.
# Host headers/structs exercise behavior; PS4 cross-builds check firmware ABI.
if(UNIX)
    set(_ps4_backend_headers "${CMAKE_BINARY_DIR}/ps4-backend-test-include")
    file(MAKE_DIRECTORY "${_ps4_backend_headers}")
    configure_file("${PROJECT_SOURCE_DIR}/ps4/sdl/SDL_config.h"
        "${_ps4_backend_headers}/SDL_config.h" COPYONLY)
    function(nocturne_ps4_backend_test name)
        add_executable(native_ps4_${name} ${ARGN}
            "${PROJECT_SOURCE_DIR}/ps4/tests/backend_support/sdl_services.c")
        target_include_directories(native_ps4_${name} PRIVATE
            "${_ps4_backend_headers}"
            "${PROJECT_SOURCE_DIR}/ps4/tests/backend_support"
            "${NOCTURNE_DEPS_SOURCE_DIR}/sdl2/include"
            "${NOCTURNE_DEPS_SOURCE_DIR}/sdl2/src"
            "${PROJECT_SOURCE_DIR}/ps4/runtime"
            "${PROJECT_SOURCE_DIR}/src/include"
            "${PROJECT_SOURCE_DIR}/src/shims")
        target_compile_definitions(native_ps4_${name} PRIVATE
            DYNAPI_NEEDS_DLOPEN=1 SDL_MAIN_HANDLED NOCTURNE_PS4=1 NOCTURNE_DISABLE_DLOG)
        set_property(TARGET native_ps4_${name} PROPERTY NOCTURNE_GAMEPAD_ONLY ON)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(native_ps4_${name} PRIVATE
                -Wno-c++11-narrowing -Wno-writable-strings -Wno-constant-conversion)
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(native_ps4_${name} PRIVATE -Wno-narrowing -Wno-write-strings)
        endif()
        if(NOT APPLE)
            target_link_libraries(native_ps4_${name} PRIVATE m)
        endif()
        add_test(NAME ps4/${name} COMMAND native_ps4_${name})
    endfunction()
    nocturne_ps4_backend_test(sdl_timer
        "${PROJECT_SOURCE_DIR}/ps4/tests/sdl_timer_tests.c"
        "${PROJECT_SOURCE_DIR}/ps4/sdl/system.c")
    nocturne_ps4_backend_test(gamepad_capture
        "${PROJECT_SOURCE_DIR}/ps4/tests/gamepad_capture_tests.cpp"
        "${PROJECT_SOURCE_DIR}/ps4/tests/backend_support/pad_driver.c"
        "${PROJECT_SOURCE_DIR}/ps4/sdl/joystick.c"
        "${PROJECT_SOURCE_DIR}/src/shims/game/gamepad.cpp"
        "${PROJECT_SOURCE_DIR}/src/game/core/menu.cpp/configureCustomKeyBindings_FUN_005138e0.keep.cpp"
        "${PROJECT_SOURCE_DIR}/src/game/core/menu.cpp/isKeyCodeValidForMode_FUN_00513720.cpp"
        "${PROJECT_SOURCE_DIR}/src/game/core/game.cpp/CGame_restoreDefaultControls_FUN_004dbbc0.keep.cpp"
        "${PROJECT_SOURCE_DIR}/src/game/core/game.cpp/CGame_resetKeyState_FUN_004dbe60.keep.cpp"
        "${PROJECT_SOURCE_DIR}/src/game/core/inivar.cpp/readIniData_FUN_004fbd90.keep.cpp"
        "${PROJECT_SOURCE_DIR}/src/game/engine/keys.cpp/CKeys_getKeyState_FUN_00502420.cpp"
        "${PROJECT_SOURCE_DIR}/src/game/engine/keys.cpp/CKeys_toggleInputMask_FUN_005024b0.cpp")
endif()
