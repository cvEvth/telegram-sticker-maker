# 测试程序与主程序同目录（共享已部署的 FFmpeg）；额外把 Qt 的 bin 加进 PATH 以找到 Qt6Test.dll
get_filename_component(QT_BIN_DIR "${Qt6_DIR}/../../../bin" ABSOLUTE)

foreach(test_name IN ITEMS tst_core tst_plan tst_editor tst_media tst_convert)
    qt_add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE tsm_core Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}")
endforeach()

# 界面交互测试：真实打开窗口（设 TSM_SNAPSHOT_DIR 可保存截图）
foreach(test_name IN ITEMS tst_editor_ui tst_mainlist_ui)
    qt_add_executable(${test_name} ${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE tsm_ui Qt6::Test)
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${QT_BIN_DIR}" TIMEOUT 120)
    add_dependencies(${test_name} TelegramStickerMaker)
endforeach()

# 依赖主程序构建后复制到 bin 的 ffmpeg / ffprobe
add_dependencies(tst_media TelegramStickerMaker)
add_dependencies(tst_convert TelegramStickerMaker)
set_tests_properties(tst_convert PROPERTIES TIMEOUT 600)
