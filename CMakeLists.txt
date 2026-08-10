cmake_minimum_required(VERSION 3.24)

# 从 src/version.h 解析 APP_VERSION "x.y.z" → major/minor/patch（唯一版本源）
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h" _version_h)
if(_version_h MATCHES "APP_VERSION \"([0-9]+)\\.([0-9]+)\\.([0-9]+)\"")
    set(VOXMIC_VERSION_MAJOR ${CMAKE_MATCH_1})
    set(VOXMIC_VERSION_MINOR ${CMAKE_MATCH_2})
    set(VOXMIC_VERSION_PATCH ${CMAKE_MATCH_3})
else()
    message(FATAL_ERROR "Cannot parse APP_VERSION from src/version.h")
endif()

project(VoxMic
    VERSION ${VOXMIC_VERSION_MAJOR}.${VOXMIC_VERSION_MINOR}.${VOXMIC_VERSION_PATCH}
    LANGUAGES C CXX)

set(VOXMIC_PRODUCT_VERSION
    "${VOXMIC_VERSION_MAJOR}.${VOXMIC_VERSION_MINOR}.${VOXMIC_VERSION_PATCH}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(VOXMIC_ENABLE_DPDFNET
    "Compile the optional DPDFNet backend and expose it in settings" OFF)
option(VOXMIC_REQUIRE_DPDFNET_PAYLOAD
    "Require verified DPDFNet runtime/model files at configure and install" OFF)
set(VOXMIC_DPDFNET_DEPS_DIR "" CACHE PATH
    "Prepared DPDFNet dependency root (contains runtime/, model/, include/)")

if(VOXMIC_ENABLE_DPDFNET)
    if(VOXMIC_DPDFNET_DEPS_DIR STREQUAL "")
        set(VOXMIC_DPDFNET_RUNTIME_DIR "")
        set(VOXMIC_DPDFNET_MODEL "")
        set(VOXMIC_DPDFNET_INCLUDE_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/third_party/dpdfnet/include")
    else()
        set(VOXMIC_DPDFNET_RUNTIME_DIR
            "${VOXMIC_DPDFNET_DEPS_DIR}/runtime")
        set(VOXMIC_DPDFNET_MODEL
            "${VOXMIC_DPDFNET_DEPS_DIR}/model/dpdfnet2_48khz_hr.onnx")
        set(VOXMIC_DPDFNET_INCLUDE_DIR
            "${VOXMIC_DPDFNET_DEPS_DIR}/include")
    endif()

    set(_dpdfnet_c_api_header
        "${VOXMIC_DPDFNET_INCLUDE_DIR}/sherpa-onnx/c-api/c-api.h")
    if(NOT EXISTS "${_dpdfnet_c_api_header}")
        message(FATAL_ERROR
            "DPDFNet is enabled but the pinned C API header is missing: ${_dpdfnet_c_api_header}")
    endif()

    set(_dpdfnet_required_files
        "${VOXMIC_DPDFNET_RUNTIME_DIR}/sherpa-onnx-c-api.dll"
        "${VOXMIC_DPDFNET_RUNTIME_DIR}/onnxruntime.dll"
        "${VOXMIC_DPDFNET_RUNTIME_DIR}/onnxruntime_providers_shared.dll"
        "${VOXMIC_DPDFNET_MODEL}"
        "${_dpdfnet_c_api_header}")
    if(VOXMIC_REQUIRE_DPDFNET_PAYLOAD)
        foreach(_dpdfnet_file IN LISTS _dpdfnet_required_files)
            if(_dpdfnet_file STREQUAL "" OR NOT EXISTS "${_dpdfnet_file}")
                message(FATAL_ERROR
                    "DPDFNet payload is required but missing: ${_dpdfnet_file}")
            endif()
        endforeach()
    endif()
endif()

add_executable(voxmic WIN32
    src/main.cpp
    src/wasapi_output.cpp
    src/device_enum.cpp
    src/socket_client.cpp
    src/adb_control.cpp
    src/tray_icon.cpp
    src/config.cpp
    src/settings_dialog.cpp
    src/mic_usage_monitor.cpp
    src/startup_registration.cpp
    src/runtime_paths.cpp
    src/dsp/dpdfnet_processor.cpp
    src/dsp/rnnoise/celt_lpc.c
    src/dsp/rnnoise/denoise.c
    src/dsp/rnnoise/kiss_fft.c
    src/dsp/rnnoise/nnet.c
    src/dsp/rnnoise/nnet_default.c
    src/dsp/rnnoise/parse_lpcnet_weights.c
    src/dsp/rnnoise/pitch.c
    src/dsp/rnnoise/rnn.c
    src/dsp/rnnoise/rnnoise_tables.c
    src/dsp/rnnoise/rnnoise_data.c
    src/voxmic.rc
)

target_include_directories(voxmic PRIVATE src/dsp/rnnoise)

if(VOXMIC_ENABLE_DPDFNET)
    target_include_directories(voxmic PRIVATE "${VOXMIC_DPDFNET_INCLUDE_DIR}")
    target_compile_definitions(voxmic PRIVATE VOXMIC_ENABLE_DPDFNET=1)
else()
    target_compile_definitions(voxmic PRIVATE VOXMIC_ENABLE_DPDFNET=0)
endif()

# /utf-8 保证含中文注释的源文件被正确解析
target_compile_options(voxmic PRIVATE
    /O2 /EHsc /W4 /WX- /wd4305 /wd4244 /utf-8)

# /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup（无控制台窗口的 main 入口）
set_target_properties(voxmic PROPERTIES
    LINK_FLAGS "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")

target_link_libraries(voxmic PRIVATE
    ws2_32 ole32 mmdevapi shell32 advapi32 comctl32 gdi32)

include(cmake/VoxMicRuntime.cmake)
voxmic_install_runtime(voxmic)

if(VOXMIC_ENABLE_DPDFNET)
    add_executable(dpdfnet_smoke
        tests/dpdfnet_smoke.cpp
        src/dsp/dpdfnet_processor.cpp)
    target_include_directories(dpdfnet_smoke PRIVATE
        src "${VOXMIC_DPDFNET_INCLUDE_DIR}")
    target_compile_definitions(dpdfnet_smoke PRIVATE VOXMIC_ENABLE_DPDFNET=1)
    target_compile_options(dpdfnet_smoke PRIVATE
        /O2 /EHsc /W4 /WX- /utf-8)

    add_executable(dpdfnet_fallback_smoke
        tests/dpdfnet_fallback_smoke.cpp
        src/dsp/dpdfnet_processor.cpp
        src/dsp/rnnoise/celt_lpc.c
        src/dsp/rnnoise/denoise.c
        src/dsp/rnnoise/kiss_fft.c
        src/dsp/rnnoise/nnet.c
        src/dsp/rnnoise/nnet_default.c
        src/dsp/rnnoise/parse_lpcnet_weights.c
        src/dsp/rnnoise/pitch.c
        src/dsp/rnnoise/rnn.c
        src/dsp/rnnoise/rnnoise_tables.c
        src/dsp/rnnoise/rnnoise_data.c)
    target_include_directories(dpdfnet_fallback_smoke PRIVATE
        src src/dsp/rnnoise "${VOXMIC_DPDFNET_INCLUDE_DIR}")
    target_compile_definitions(dpdfnet_fallback_smoke PRIVATE
        VOXMIC_ENABLE_DPDFNET=1)
    target_compile_options(dpdfnet_fallback_smoke PRIVATE
        /O2 /EHsc /W4 /WX- /wd4305 /wd4244 /utf-8)

    add_executable(dpdfnet_pipeline_switch_smoke
        tests/dpdfnet_pipeline_switch_smoke.cpp
        src/dsp/dpdfnet_processor.cpp
        src/dsp/rnnoise/celt_lpc.c
        src/dsp/rnnoise/denoise.c
        src/dsp/rnnoise/kiss_fft.c
        src/dsp/rnnoise/nnet.c
        src/dsp/rnnoise/nnet_default.c
        src/dsp/rnnoise/parse_lpcnet_weights.c
        src/dsp/rnnoise/pitch.c
        src/dsp/rnnoise/rnn.c
        src/dsp/rnnoise/rnnoise_tables.c
        src/dsp/rnnoise/rnnoise_data.c)
    target_include_directories(dpdfnet_pipeline_switch_smoke PRIVATE
        src src/dsp/rnnoise "${VOXMIC_DPDFNET_INCLUDE_DIR}")
    target_compile_definitions(dpdfnet_pipeline_switch_smoke PRIVATE
        VOXMIC_ENABLE_DPDFNET=1 VOXMIC_DPDFNET_TEST_HOOKS=1)
    target_compile_options(dpdfnet_pipeline_switch_smoke PRIVATE
        /O2 /EHsc /W4 /WX- /wd4305 /wd4244 /utf-8)
endif()
