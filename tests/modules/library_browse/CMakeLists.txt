# ---- 音樂庫瀏覽與搜尋模組的測試（標籤 LIB）----
# 測試音樂庫在測試時產生音檔再掃描（共用核心測試的產生器與掃描），所以連結 auralis_core
set(auralis_lib_test_support
    lib_test_helpers.h
    ${PROJECT_SOURCE_DIR}/tests/core/library/library_fixtures.cpp
    ${PROJECT_SOURCE_DIR}/tests/core/audio/signal_writer.cpp
    ../../utf8.manifest
)
find_package(TagLib CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

function(auralis_lib_test_target target)
    target_link_libraries(${target} PRIVATE auralis_core auralis_mod_library_browse_qml
        Catch2::Catch2WithMain TagLib::tag)
    target_include_directories(${target} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/modules/library_browse
        ${PROJECT_SOURCE_DIR}/tests/core/library
        ${PROJECT_SOURCE_DIR}/tests/core/audio
        ${PROJECT_SOURCE_DIR}/tests/ui
        ${FFMPEG_INCLUDE_DIRS})
    target_link_directories(${target} PRIVATE ${FFMPEG_LIBRARY_DIRS})
    target_link_libraries(${target} PRIVATE ${FFMPEG_LIBRARIES})
    target_compile_definitions(${target} PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}")
endfunction()

add_executable(auralis_lib_tests
    test_search_criteria.cpp
    test_search_controller.cpp
    # QML 單例 LibrarySearch：連結模組本身（帶進 Auralis.Lib 外掛）
    test_search_results.cpp
    # 「所有曲目」頁的 C++ 部分（T-21）：用介面測試的假播放服務
    test_track_list.cpp
    # 專輯頁的 C++ 部分（T-22）
    test_albums.cpp
    # 藝人頁與資料夾頁的 C++ 部分（T-23）
    test_artists.cpp
    test_folders.cpp
    # 專輯詳細頁的 C++ 部分（T-24）
    test_album_detail.cpp
    # 清單的選取、拖放與右鍵選單的 C++ 部分（T-25）
    test_track_selection.cpp
    # 曲目清單的欄位與欄寬設定（T-90）
    test_track_columns.cpp
    lib_memory_library.h
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ${auralis_lib_test_support}
)
auralis_lib_test_target(auralis_lib_tests)
target_link_libraries(auralis_lib_tests PRIVATE auralis_mod_library_browse)
catch_discover_tests(auralis_lib_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS LIB
)

# 頁面的 Qt Quick Test（qml/tst_*.qml，T-22 起）：專輯頁的鍵盤操作與三種檢視；T-23 藝人、資料夾頁；
# T-24 專輯詳細頁。
# 頁面用到 Auralis.Ui 的共用元件與核心的介面服務，所以連結介面的 QML 模組；用 offscreen 平台跑，
# 不需要螢幕。結果寫到測試暫存的檔案（視窗程式的輸出 ctest 收不到），失敗時看那個檔案
find_package(Qt6 REQUIRED COMPONENTS QuickTest)
add_executable(auralis_lib_quick_tests
    lib_quick_test_main.cpp
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ../../utf8.manifest
)
target_link_libraries(auralis_lib_quick_tests PRIVATE auralis_core auralis_ui auralis_uiplugin
    auralis_mod_library_browse_qml auralis_mod_library_browse_qmlplugin Qt6::QuickTest)
target_include_directories(auralis_lib_quick_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/modules/library_browse
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/tests/ui)
target_compile_definitions(auralis_lib_quick_tests PRIVATE
    AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
file(MAKE_DIRECTORY "${AURALIS_TEST_TMP}/t-22-quick")
add_test(NAME LIB.quick_albums
    COMMAND auralis_lib_quick_tests -o "${AURALIS_TEST_TMP}/t-22-quick/result.txt,txt")
set_tests_properties(LIB.quick_albums PROPERTIES
    LABELS LIB
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${AURALIS_QT_BIN}")

# 搜尋效能（AC-09-1，標籤 perf；STACK §6：ctest --preset windows-release -L perf -R search）
# 與專輯牆的 UI 執行緒負擔（AC-10-2）。
# 量時間的測試不和其他測試同時跑
add_executable(auralis_lib_perf_tests
    test_search_perf.cpp
    # 專輯牆在 UI 執行緒的負擔（AC-10-2，T-22；ctest --preset windows-release -L perf -R AC-10-2）
    test_albums_perf.cpp
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ${auralis_lib_test_support}
)
auralis_lib_test_target(auralis_lib_perf_tests)
target_compile_definitions(auralis_lib_perf_tests PRIVATE
    AURALIS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
catch_discover_tests(auralis_lib_perf_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS perf TIMEOUT 3600 RUN_SERIAL TRUE
)
