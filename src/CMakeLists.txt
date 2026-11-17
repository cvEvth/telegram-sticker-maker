# 核心库：不依赖 Widgets，单元测试直接链接它
qt_add_library(tsm_core STATIC
    app/AiMatcher.cpp       app/AiMatcher.h
    app/AppPaths.cpp        app/AppPaths.h
    app/AppSettings.cpp     app/AppSettings.h
    app/BatchConverter.cpp  app/BatchConverter.h
    app/Exporter.cpp        app/Exporter.h
    app/ImportSource.cpp    app/ImportSource.h
    app/Language.cpp        app/Language.h
    app/ListLoader.cpp      app/ListLoader.h
    app/Logger.cpp          app/Logger.h
    app/SessionStore.cpp    app/SessionStore.h
    app/UpdateChecker.cpp   app/UpdateChecker.h
    core/AiEmoji.cpp        core/AiEmoji.h
    core/CropMath.cpp       core/CropMath.h
    core/EncodePlan.cpp     core/EncodePlan.h
    core/Format.cpp         core/Format.h
    core/ImageHeaders.cpp   core/ImageHeaders.h
    core/Keywords.cpp       core/Keywords.h
    core/MediaInfo.h
    core/MediaKinds.cpp     core/MediaKinds.h
    core/OutputNaming.cpp   core/OutputNaming.h
    core/Spec.h
    core/StickerList.cpp    core/StickerList.h
    core/StickerTask.cpp    core/StickerTask.h
    core/TaskListModel.cpp  core/TaskListModel.h
    core/TimelineState.cpp  core/TimelineState.h
    io/OutputPackager.cpp   io/OutputPackager.h
    io/ZipReader.cpp        io/ZipReader.h
    io/ZipWriter.cpp        io/ZipWriter.h
    media/AiFrameGrabber.cpp media/AiFrameGrabber.h
    media/FFmpegTools.cpp   media/FFmpegTools.h
    media/FfmpegRun.cpp     media/FfmpegRun.h
    media/FilterGraph.cpp   media/FilterGraph.h
    media/MediaProbe.cpp    media/MediaProbe.h
    media/ProcessRunner.cpp media/ProcessRunner.h
    media/ProxyCache.cpp    media/ProxyCache.h
    media/SourceInput.cpp   media/SourceInput.h
    media/SourceTrimmer.cpp media/SourceTrimmer.h
    media/StickerEncoder.cpp media/StickerEncoder.h
    media/ThumbnailService.cpp media/ThumbnailService.h
    media/WebmDuration.cpp  media/WebmDuration.h
    media/WebpEncoder.cpp   media/WebpEncoder.h
)
target_include_directories(tsm_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(tsm_core
    PUBLIC
        Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Network
    PRIVATE
        WebP::webp WebP::webpdemux
        libzip::zip
        FFmpeg::avformat FFmpeg::avcodec FFmpeg::avfilter FFmpeg::avutil FFmpeg::swscale FFmpeg::swresample)

# 界面库：主程序与界面交互测试共用
qt_add_library(tsm_ui STATIC
    ui/AiSettingsDialog.cpp ui/AiSettingsDialog.h
    ui/BotPromo.cpp      ui/BotPromo.h
    ui/ConversionDoneDialog.cpp ui/ConversionDoneDialog.h
    ui/DropOverlay.cpp   ui/DropOverlay.h
    ui/DropZone.cpp      ui/DropZone.h
    ui/EmojiDialog.cpp   ui/EmojiDialog.h
    ui/FlowLayout.cpp    ui/FlowLayout.h
    ui/HeaderBar.cpp     ui/HeaderBar.h
    ui/KeywordDialog.cpp ui/KeywordDialog.h
    ui/LogPanel.cpp      ui/LogPanel.h
    ui/MainWindow.cpp    ui/MainWindow.h
    ui/SettingsPanel.cpp ui/SettingsPanel.h
    ui/TaskTableDelegates.cpp ui/TaskTableDelegates.h
    ui/TaskTableView.cpp ui/TaskTableView.h
    ui/Theme.cpp         ui/Theme.h
    ui/editor/ClipEditorDialog.cpp     ui/editor/ClipEditorDialog.h
    ui/editor/ClipListPanel.cpp        ui/editor/ClipListPanel.h
    ui/editor/EditorSettingsDialog.cpp ui/editor/EditorSettingsDialog.h
    ui/editor/PreviewCanvas.cpp        ui/editor/PreviewCanvas.h
    ui/editor/TimelineWidgets.cpp      ui/editor/TimelineWidgets.h
)
qt_add_resources(tsm_ui "app_resources"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/styles/app.qss"
          "${PROJECT_SOURCE_DIR}/resources/icons/app.ico")
target_link_libraries(tsm_ui PUBLIC tsm_core Qt6::Widgets Qt6::Multimedia Qt6::Network)

# 翻译：update_translations 目标用 lupdate 从两个库的源码更新 .ts，构建时 lrelease 生成 .qm 并嵌入资源 :/i18n
qt_add_translations(tsm_ui
    SOURCE_TARGETS tsm_core tsm_ui
    TS_FILES "${PROJECT_SOURCE_DIR}/translations/tsm_en.ts"
             "${PROJECT_SOURCE_DIR}/translations/tsm_ru.ts"
    NO_GENERATE_PLURALS_TS_FILE
    RESOURCE_PREFIX "/i18n"
    LUPDATE_OPTIONS -no-obsolete -locations none)

# Windows 是普通 exe，macOS 打成 .app（Finder 里双击、拖进「应用程序」）
if(WIN32)
    set(TSM_EXECUTABLE_KIND WIN32)
elseif(APPLE)
    set(TSM_EXECUTABLE_KIND MACOSX_BUNDLE)
else()
    set(TSM_EXECUTABLE_KIND "")
endif()

qt_add_executable(TelegramStickerMaker ${TSM_EXECUTABLE_KIND} main.cpp)
if(WIN32)
    # exe 文件图标与版本信息（资源管理器、任务栏、快捷方式、文件属性）
    set(TSM_RC_ICON "${PROJECT_SOURCE_DIR}/resources/icons/app.ico")
    configure_file("${PROJECT_SOURCE_DIR}/resources/icons/app.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(TelegramStickerMaker PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()
if(APPLE)
    # .app 图标与 Info.plist；版本号来自 CMakeLists.txt 的 TSM_VERSION
    target_sources(TelegramStickerMaker PRIVATE "${PROJECT_SOURCE_DIR}/resources/icons/AppIcon.icns")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/resources/icons/AppIcon.icns"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    # 第三方组件许可证说明（Qt / FFmpeg 是 LGPL）
    target_sources(TelegramStickerMaker PRIVATE "${PROJECT_SOURCE_DIR}/packaging/macos/THIRD_PARTY_NOTICES.txt")
    set_source_files_properties("${PROJECT_SOURCE_DIR}/packaging/macos/THIRD_PARTY_NOTICES.txt"
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    set_target_properties(TelegramStickerMaker PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "Telegram Sticker Maker"
        MACOSX_BUNDLE_GUI_IDENTIFIER "top.cvevth.telegram-sticker-maker"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
        MACOSX_BUNDLE_COPYRIGHT "cvEvth")
endif()
target_link_libraries(TelegramStickerMaker PRIVATE tsm_ui)
target_compile_definitions(TelegramStickerMaker PRIVATE APP_VERSION="${PROJECT_VERSION}")

# 构建后把 FFmpeg 与 Qt 运行库部署到程序目录，双击即可运行
add_custom_command(TARGET TelegramStickerMaker POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${FFMPEG_RUNTIME_FILES} "$<TARGET_FILE_DIR:TelegramStickerMaker>"
    COMMENT "Copying FFmpeg runtime"
    VERBATIM)
if(APPLE)
    # 测试程序在 build/bin 里，按「程序目录优先」找 ffmpeg，所以这里也放一份。
    # 用 ALL 目标而不是 POST_BUILD：FFmpeg 更新但主程序没有重新链接时也要刷新。
    add_custom_target(tsm_deploy_ffmpeg ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${FFMPEG_RUNTIME_FILES}
            "$<TARGET_BUNDLE_CONTENT_DIR:TelegramStickerMaker>/MacOS"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${FFMPEG_RUNTIME_FILES}
            "${CMAKE_BINARY_DIR}/bin"
        DEPENDS ${FFMPEG_RUNTIME_FILES} TelegramStickerMaker
        COMMENT "Copying FFmpeg runtime"
        VERBATIM)
endif()
if(TARGET Qt6::windeployqt AND TARGET Qt6::qtpaths)
    # windeployqt 默认从 PATH 找 qtpaths，这里显式指定，构建时不依赖 PATH。
    # 它合并翻译需要 qttools 的 lconvert，所以关掉翻译部署，改为下面直接复制 .qm。
    get_filename_component(QT_TRANSLATIONS_DIR "${Qt6_DIR}/../../../translations" ABSOLUTE)
    add_custom_command(TARGET TelegramStickerMaker POST_BUILD
        COMMAND "$<TARGET_FILE:Qt6::windeployqt>"
            --qtpaths "$<TARGET_FILE:Qt6::qtpaths>"
            --no-compiler-runtime --no-system-d3d-compiler --no-system-dxc-compiler --no-opengl-sw --no-quick-import
            --no-translations --verbose 0
            "$<TARGET_FILE:TelegramStickerMaker>"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:TelegramStickerMaker>/translations"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_TRANSLATIONS_DIR}/qtbase_zh_CN.qm"
            "${QT_TRANSLATIONS_DIR}/qtmultimedia_zh_CN.qm"
            "${QT_TRANSLATIONS_DIR}/qtbase_ru.qm"
            "${QT_TRANSLATIONS_DIR}/qtmultimedia_ru.qm"
            "$<TARGET_FILE_DIR:TelegramStickerMaker>/translations"
        COMMENT "Deploying Qt runtime"
        VERBATIM)
endif()
if(APPLE)
    # macOS：macdeployqt 把 Qt 框架和插件拷进 .app；Qt 自带翻译放到 Resources/translations
    find_program(TSM_MACDEPLOYQT NAMES macdeployqt
        HINTS "${Qt6_DIR}/../../../bin" "${QT6_INSTALL_PREFIX}/bin" REQUIRED)
    get_filename_component(QT_TRANSLATIONS_DIR "${Qt6_DIR}/../../../translations" ABSOLUTE)
    add_custom_command(TARGET TelegramStickerMaker POST_BUILD
        COMMAND "${TSM_MACDEPLOYQT}" "$<TARGET_BUNDLE_DIR:TelegramStickerMaker>" -always-overwrite -verbose=1
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_BUNDLE_CONTENT_DIR:TelegramStickerMaker>/Resources/translations"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${QT_TRANSLATIONS_DIR}/qtbase_zh_CN.qm"
            "${QT_TRANSLATIONS_DIR}/qtmultimedia_zh_CN.qm"
            "${QT_TRANSLATIONS_DIR}/qtbase_ru.qm"
            "${QT_TRANSLATIONS_DIR}/qtmultimedia_ru.qm"
            "$<TARGET_BUNDLE_CONTENT_DIR:TelegramStickerMaker>/Resources/translations"
        COMMENT "Deploying Qt runtime (macdeployqt)"
        VERBATIM)
endif()
