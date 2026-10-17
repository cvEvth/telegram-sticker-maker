cmake_minimum_required(VERSION 3.25)

project(TelegramStickerMaker
    VERSION 0.1.0
    DESCRIPTION "Batch converter for Telegram stickers"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 所有可执行文件放在同一目录，共享已部署的 Qt / FFmpeg 运行库
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

if(MSVC)
    add_compile_options(/utf-8 /permissive- /W3)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
endif()

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network Concurrent Test)
qt_standard_project_setup()

find_package(WebP CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)
include(cmake/FFmpeg.cmake)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
