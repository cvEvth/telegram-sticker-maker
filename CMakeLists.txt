cmake_minimum_required(VERSION 3.25)

# 版本号：发版时 CI 用标签里的版本（环境变量 TSM_VERSION，如 v1.2.0 → 1.2.0）覆盖这里的默认值
if(DEFINED ENV{TSM_VERSION} AND NOT "$ENV{TSM_VERSION}" STREQUAL "")
    set(TSM_VERSION "$ENV{TSM_VERSION}")
else()
    set(TSM_VERSION 0.3.5)
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

find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Multimedia Network Concurrent Test LinguistTools)

# macOS 15+ 起 AGL.framework 只剩一个没有二进制的空壳，Qt 6.8 的 FindWrapOpenGL 仍会去链接它，
# 链接直接失败。Qt 只用 AGL 兼容很老的 OpenGL 代码，这里从链接项里去掉即可。
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
    get_target_property(_tsm_opengl_libs WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
    if(_tsm_opengl_libs)
        set(_tsm_opengl_kept)
        foreach(_tsm_lib IN LISTS _tsm_opengl_libs)
            if(_tsm_lib STREQUAL "-framework AGL" OR _tsm_lib MATCHES "AGL(\\.framework)?$")
                continue()
            endif()
            list(APPEND _tsm_opengl_kept "${_tsm_lib}")
        endforeach()
        set_target_properties(WrapOpenGL::WrapOpenGL PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_tsm_opengl_kept}")
    endif()
endif()

# 界面文字直接用简体中文写在代码里，英语 / 俄语走 Qt Linguist 翻译（translations/*.ts）
qt_standard_project_setup(I18N_SOURCE_LANGUAGE zh_CN I18N_TRANSLATED_LANGUAGES en ru)

find_package(WebP CONFIG REQUIRED)
find_package(libzip CONFIG REQUIRED)
include(cmake/FFmpeg.cmake)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
