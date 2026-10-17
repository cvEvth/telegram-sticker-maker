# 从 FFmpeg 预编译共享库目录（bin / include / lib）创建导入目标 FFmpeg::<lib>，
# 并收集运行时文件（DLL + ffmpeg.exe / ffprobe.exe）供构建后复制到程序目录。

set(FFMPEG_ROOT "$ENV{FFMPEG_ROOT}" CACHE PATH "Root of an FFmpeg shared build (bin/include/lib)")

if(NOT EXISTS "${FFMPEG_ROOT}/include/libavcodec/avcodec.h")
    message(FATAL_ERROR "FFmpeg not found: set FFMPEG_ROOT to an FFmpeg shared build (got '${FFMPEG_ROOT}')")
endif()

if(NOT WIN32)
    # TODO(mac): 改用 pkg-config 或 vcpkg 提供的 FFmpeg
    message(FATAL_ERROR "cmake/FFmpeg.cmake currently supports Windows builds only")
endif()

set(FFMPEG_COMPONENTS avformat avcodec avfilter avutil swscale swresample)

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
