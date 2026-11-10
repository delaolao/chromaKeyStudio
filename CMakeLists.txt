cmake_minimum_required(VERSION 3.20)
project(ChromaKeyStudio VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 静态链接 FFmpeg, 让 exe 不依赖外部 ffmpeg.exe
# 关闭调试符号, 并让链接器丢弃没用到的段 —— 体积能小一大截
add_compile_options(-O2 -ffunction-sections -fdata-sections -fno-exceptions)
add_link_options(-Wl,--gc-sections -s)

set(FFMPEG_ROOT "${CMAKE_SOURCE_DIR}/_ffbuild/out" CACHE PATH "minimal ffmpeg build")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Svg)

# ---------------------------------------------------------------- 核心库
add_library(ckcore STATIC
    src/core/keying.cpp
    src/core/media.cpp
    src/core/export.cpp
)
target_include_directories(ckcore PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${FFMPEG_ROOT}/include
)

set(FF_LIBS
    ${FFMPEG_ROOT}/lib/libavformat.a
    ${FFMPEG_ROOT}/lib/libavcodec.a
    ${FFMPEG_ROOT}/lib/libswscale.a
    ${FFMPEG_ROOT}/lib/libswresample.a
    ${FFMPEG_ROOT}/lib/libavutil.a
    ${FFMPEG_ROOT}/lib/libvpx.a
    ${FFMPEG_ROOT}/lib/libopus.a
    ${FFMPEG_ROOT}/lib/libz.a
)
target_link_libraries(ckcore PUBLIC ${FF_LIBS}
    ws2_32 secur32 bcrypt ole32 oleaut32 uuid user32 advapi32 shell32
)

# ---------------------------------------------------------------- GUI
add_executable(ChromaKeyStudio WIN32
    src/main.cpp
    src/ui/mainwindow.cpp
    src/ui/aboutdialog.cpp
    src/ui/previewwidget.cpp
    src/ui/theme.cpp
)
target_link_libraries(ChromaKeyStudio PRIVATE ckcore Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg)

# ---------------------------------------------------------------- 无界面导出工具(调试用)
add_executable(ckcli tests/cli_export.cpp)
target_link_libraries(ckcli PRIVATE ckcore)
