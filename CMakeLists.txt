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
