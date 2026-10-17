cmake_minimum_required(VERSION 3.25)

# 版本号：发版时 CI 用标签里的版本（环境变量 TSM_VERSION，如 v1.2.0 → 1.2.0）覆盖这里的默认值
if(DEFINED ENV{TSM_VERSION} AND NOT "$ENV{TSM_VERSION}" STREQUAL "")
    set(TSM_VERSION "$ENV{TSM_VERSION}")
else()
    set(TSM_VERSION 0.1.0)
endif()

project(TelegramStickerMaker
    VERSION ${TSM_VERSION}
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
