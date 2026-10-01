# Compile the retained renderer subjects and fixtures with the production shims.
# Explicit subjects preserve the boundary between pure, recorded and native GL.
set(_renderer_tests "${NOCTURNE_TESTS_DIR}/shims/renderer")
set(_renderer "${NOCTURNE_SHIMS_DIR}/renderer")
function(nocturne_recomp_renderer_test target)
    target_include_directories(${target} PRIVATE
        "${NOCTURNE_TESTS_DIR}" "${_renderer_tests}"
        "${NOCTURNE_SHIMS_DIR}" "${NOCTURNE_INCLUDE_DIR}"
        "${PROJECT_SOURCE_DIR}/src/platform")
    target_link_libraries(${target} PRIVATE SDL2::SDL2)
    target_compile_features(${target} PRIVATE cxx_std_23)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(${target} PRIVATE -fwrapv -fno-strict-aliasing)
    # Recovered headers spell signed 32-bit bit patterns with hex literals and
    # retain char* APIs. Match the runtime's accepted reconstruction syntax.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target} PRIVATE
            -Wno-c++11-narrowing -Wno-writable-strings)
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(${target} PRIVATE -Wno-narrowing -Wno-write-strings)
    endif()
    target_compile_definitions(${target} PRIVATE NOCTURNE_DUMP_TOOLS=0)
endfunction()

foreach(_part batch state texture vertex)
    add_executable(native_trigl_${_part}
        "${_renderer_tests}/test_trigl_${_part}.cpp"
        "${_renderer}/trigl_${_part}.cpp")
    nocturne_recomp_renderer_test(native_trigl_${_part})
    add_test(NAME renderer/trigl_${_part} COMMAND native_trigl_${_part})
    set_tests_properties(renderer/trigl_${_part} PROPERTIES LABELS renderer)
endforeach()
target_sources(native_trigl_vertex PRIVATE "${_renderer}/trigl_state.cpp")

add_library(native_trigl_recorded STATIC
    "${_renderer}/trigl_gl.cpp" "${_renderer}/trigl_device.cpp"
    "${_renderer}/trigl_exports.cpp" "${_renderer}/trigl_batch.cpp"
    "${_renderer}/trigl_state.cpp" "${_renderer}/trigl_texture.cpp"
    "${_renderer}/trigl_vertex.cpp" "${_renderer_tests}/gl_recorder.cpp"
    "${_renderer_tests}/gl_present_support.cpp"
    "${_renderer_tests}/golden_trace_support.cpp")
nocturne_recomp_renderer_test(native_trigl_recorded)
target_compile_definitions(native_trigl_recorded PRIVATE
    NOCTURNE_TEST_DATA_DIR="${_renderer_tests}/golden")
foreach(_part gl_sequence exports_sequence golden_sequence)
    add_executable(native_trigl_${_part} "${_renderer_tests}/test_trigl_${_part}.cpp")
    nocturne_recomp_renderer_test(native_trigl_${_part})
    target_link_libraries(native_trigl_${_part} PRIVATE native_trigl_recorded)
    add_test(NAME renderer/trigl_${_part} COMMAND native_trigl_${_part})
    set_tests_properties(renderer/trigl_${_part} PROPERTIES LABELS renderer)
endforeach()

add_library(native_gl_subjects STATIC
    "${PROJECT_SOURCE_DIR}/src/platform/smoke_probe.cpp"
    "${NOCTURNE_SHIMS_DIR}/gl/gl_api.cpp"
    "${NOCTURNE_SHIMS_DIR}/gl/gl_blit.cpp"
    "${NOCTURNE_SHIMS_DIR}/gl/gl_present.cpp"
    "${_renderer}/trigl_gl.cpp" "${_renderer}/trigl_batch.cpp")
nocturne_recomp_renderer_test(native_gl_subjects)
foreach(_part retina_mapping gl_pixels)
    add_executable(native_${_part} "${CMAKE_CURRENT_LIST_DIR}/${_part}.cpp")
    nocturne_recomp_renderer_test(native_${_part})
    target_link_libraries(native_${_part} PRIVATE native_gl_subjects)
    add_test(NAME renderer/${_part} COMMAND native_${_part})
    set_tests_properties(renderer/${_part} PROPERTIES LABELS renderer)
endforeach()
set_tests_properties(renderer/gl_pixels PROPERTIES LABELS "renderer;native-display" RUN_SERIAL TRUE)

add_executable(native_resolution_gate "${CMAKE_CURRENT_LIST_DIR}/resolution_gate.cpp"
    "${NOCTURNE_SHIMS_DIR}/game/resolution.cpp")
nocturne_recomp_renderer_test(native_resolution_gate)
add_test(NAME renderer/resolution_gate COMMAND native_resolution_gate)
set_tests_properties(renderer/resolution_gate PROPERTIES LABELS renderer)
