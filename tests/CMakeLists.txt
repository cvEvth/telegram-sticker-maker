# 测试程序与主程序同目录（共享已部署的 FFmpeg）；额外把 Qt 的 bin 加进 PATH 以找到 Qt6Test.dll
get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)

foreach(test_name IN ITEMS tst_core tst_plan tst_editor tst_media tst_convert)
    qt_add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE tsm_core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}")
endforeach()

# 界面交互测试：需要窗口与多媒体，直接编译界面源文件
qt_add_executable(tst_editor_ui tst_editor_ui.cpp
    ../src/ui/Theme.cpp
    ../src/ui/editor/ClipEditorDialog.cpp
    ../src/ui/editor/ClipListPanel.cpp
    ../src/ui/editor/EditorSettingsDialog.cpp
    ../src/ui/editor/PreviewCanvas.cpp
    ../src/ui/editor/TimelineWidgets.cpp)
target_include_directories(tst_editor_ui PRIVATE "${PROJECT_SOURCE_DIR}/src")
qt_add_resources(tst_editor_ui "test_resources" PREFIX "/" BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES "${PROJECT_SOURCE_DIR}/resources/styles/dark.qss")
target_link_libraries(tst_editor_ui PRIVATE tsm_core Qt6::Widgets Qt6::Multimedia Qt6::Test)
add_test(NAME tst_editor_ui COMMAND tst_editor_ui)
set_tests_properties(tst_editor_ui PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}" TIMEOUT 120)
add_dependencies(tst_editor_ui TelegramStickerMaker)

# 依赖主程序构建后复制到 bin 的 ffmpeg / ffprobe
add_dependencies(tst_media TelegramStickerMaker)
add_dependencies(tst_convert TelegramStickerMaker)
set_tests_properties(tst_convert PROPERTIES TIMEOUT 600)
