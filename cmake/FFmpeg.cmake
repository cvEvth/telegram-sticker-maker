# 收集 FFmpeg 运行时文件（ffmpeg / ffprobe，Windows 上还有 DLL），供构建后复制到程序目录。
#
# Windows：从 FFmpeg 预编译共享库目录（bin / include / lib）创建导入目标 FFmpeg::<lib>。
# macOS：程序只通过命令行调用 ffmpeg / ffprobe，不链接 libav*，所以这里只定位可执行文件。
#        FFMPEG_ROOT 指向任意带 bin/ffmpeg、bin/ffprobe 的目录；不设置时从 PATH 里找。

set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}" CACHE PATH "Root of an FFmpeg build (bin/include/lib)")

set(FFMPEG_COMPONENTS avformat avcodec avfilter avutil swscale swresample)

if(APPLE)
    if(FFMPEG_ROOT)
        set(_ffmpeg "${FFMPEG_ROOT}/bin/ffmpeg")
        set(_ffprobe "${FFMPEG_ROOT}/bin/ffprobe")
    else()
        find_program(TSM_FFMPEG_EXECUTABLE NAMES ffmpeg)
        find_program(TSM_FFPROBE_EXECUTABLE NAMES ffprobe)
        set(_ffmpeg "${TSM_FFMPEG_EXECUTABLE}")
        set(_ffprobe "${TSM_FFPROBE_EXECUTABLE}")
    endif()

    if(NOT _ffmpeg OR NOT _ffprobe OR NOT EXISTS "${_ffmpeg}" OR NOT EXISTS "${_ffprobe}")
        message(FATAL_ERROR
            "FFmpeg not found: point FFMPEG_ROOT at a directory with bin/ffmpeg and bin/ffprobe, "
            "or put both on PATH (got FFMPEG_ROOT='${FFMPEG_ROOT}')")
    endif()
    message(STATUS "FFmpeg runtime: ${_ffmpeg} / ${_ffprobe}")

    # 程序不需要链接 libav*，导入目标留作占位，保持 src/CMakeLists.txt 的写法跨平台。
    foreach(component IN LISTS FFMPEG_COMPONENTS)
        add_library(FFmpeg::${component} INTERFACE IMPORTED)
    endforeach()

    set(FFMPEG_RUNTIME_FILES "${_ffmpeg}" "${_ffprobe}")
    return()
endif()

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg not found: set FFMPEG_ROOT to an FFmpeg shared build (got '${FFMPEG_ROOT}')")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "cmake/FFmpeg.cmake supports Windows and macOS; port the remaining platforms here")
endif()

foreach(component IN LISTS FFMPEG_COMPONENTS)
    file(GLOB _dll "${FFMPEG_ROOT}/bin/${component}-*.dll")
    if(NOT _dll OR NOT EXISTS "${FFMPEG_ROOT}/lib/${component}.lib")
        message(FATAL_ERROR "FFmpeg component '${component}' missing under ${FFMPEG_ROOT}")
    endif()
    add_library(FFmpeg::${component} SHARED IMPORTED)
    set_target_properties(FFmpeg::${component} PROPERTIES
        IMPORTED_IMPLIB "${FFMPEG_ROOT}/lib/${component}.lib"
        IMPORTED_LOCATION "${_dll}"
        INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_ROOT}/include")
endforeach()

file(GLOB FFMPEG_RUNTIME_FILES
    "${FFMPEG_ROOT}/bin/*.dll"
    "${FFMPEG_ROOT}/bin/ffmpeg.exe"
    "${FFMPEG_ROOT}/bin/ffprobe.exe")
