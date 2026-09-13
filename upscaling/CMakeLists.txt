if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Built-in upscaling requires a Win32 MSVC build")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(NEURAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(NEURAL_GEN "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_executable(CompileUpscalingShaders "${NEURAL_ROOT}/tests/upscaling/CompileShaders.cpp")
target_link_libraries(CompileUpscalingShaders PRIVATE d3dcompiler)
file(GLOB NEURAL_HLSL CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.hlsl")
add_custom_command(OUTPUT "${NEURAL_GEN}/NeuralShaders.h"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NEURAL_GEN}/shaders"
    COMMAND ${Python3_EXECUTABLE} -B "${NEURAL_ROOT}/tools/generate-cunny.py" --out "${NEURAL_GEN}/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${NEURAL_HLSL} "${NEURAL_GEN}/shaders/"
    COMMAND $<TARGET_FILE:CompileUpscalingShaders> "${NEURAL_GEN}/shaders"
    COMMAND ${Python3_EXECUTABLE} -B "${NEURAL_ROOT}/tools/embed-upscaling-shaders.py" "${NEURAL_GEN}/shaders" "${NEURAL_GEN}/NeuralShaders.h"
    DEPENDS CompileUpscalingShaders ${NEURAL_HLSL}
        "${NEURAL_ROOT}/tools/generate-cunny.py" "${NEURAL_ROOT}/tools/embed-upscaling-shaders.py"
        "${NEURAL_ROOT}/third_party/cunny/CuNNy-fast-NVL.hlsl"
        "${NEURAL_ROOT}/third_party/cunny/CuNNy-veryfast-NVL.hlsl"
    VERBATIM)
file(GLOB D3D8TO9_SOURCES CONFIGURE_DEPENDS "${NEURAL_ROOT}/third_party/d3d8to9/source/*.cpp")
add_library(d3d8 SHARED ${D3D8TO9_SOURCES} Proxy.cpp UpscaleConfig.cpp UpscaleRenderer.cpp
    "${NEURAL_GEN}/NeuralShaders.h" "${NEURAL_ROOT}/third_party/d3d8to9/res/d3d8.def")
target_include_directories(d3d8 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}" "${NEURAL_GEN}"
    "${NEURAL_ROOT}/ezorsia" "${NEURAL_ROOT}/third_party/d3d8to9/source")
target_compile_definitions(d3d8 PRIVATE D3D8TO9NOLOG NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(d3d8 PRIVATE /W3 /EHsc /utf-8)
target_link_libraries(d3d8 PRIVATE d3d9 user32 gdi32 advapi32)

set_target_properties(d3d8 PROPERTIES OUTPUT_NAME "BeiDouUpscale")
