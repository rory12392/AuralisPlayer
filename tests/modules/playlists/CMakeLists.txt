# ---- 播放清單模組的測試（標籤 PL）----
# 用真的 SQLite 資料庫（核心的升級與 ModuleDatabase），所以連結 auralis_core
add_executable(auralis_pl_tests
    pl_test_helpers.h
    # 播放清單儲存與單例 Playlists 的 C++ 部分（T-50）
    test_playlist_store.cpp
    test_playlists_controller.cpp
    # 播放清單頁的曲目清單（T-55）：用介面測試的假播放服務
    test_playlist_tracks.cpp
    # 匯入 m3u／m3u8／pls（T-51）：回歸樣本在 tests/fixtures/playlists-public（合成，T-100）；
    # 私有真樣本 tests/fixtures/playlists（唯讀）不在時，標 [private-fixtures] 的測試 SKIP
    pl_import_test_helpers.h
    test_playlist_import.cpp
    test_playlist_parse.cpp
    # .fpl 匯入（T-53）：回歸樣本 test1.fpl、真樣本 test3.fpl；壞檔用程式產生
    pl_fpl_test_helpers.h
    test_playlist_fpl.cpp
    test_playlist_fpl_bad.cpp
    # m3u 裡的 .cue 展開成整張 CUE、內嵌 cue 的重新檢查（T-56）：真樣本 test3.m3u8＋假音樂庫
    test_playlist_cue.cpp
    # 找不到的檔案與重新對應（T-52）：QSignalSpy 用 Qt6::Test
    test_playlist_remap.cpp
    # 匯出 .m3u8（T-54）：往返用 T-51 的匯入讀回；寫檔只寫測試暫存
    pl_export_test_helpers.h
    test_playlist_export.cpp
    test_playlist_exporter.cpp
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ../../utf8.manifest
)
find_package(Qt6 REQUIRED COMPONENTS Test)
target_link_libraries(auralis_pl_tests PRIVATE auralis_core auralis_mod_playlists_qml
    Catch2::Catch2WithMain Qt6::Test)
target_include_directories(auralis_pl_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/modules/playlists
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/tests/ui)
target_compile_definitions(auralis_pl_tests PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    AURALIS_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
catch_discover_tests(auralis_pl_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS PL
)

# 側欄播放清單區與右鍵「加入播放清單 ▸」的 Qt Quick Test（qml/tst_*.qml，T-50）：鍵盤、右鍵選單、拖放。
# 用到 Auralis.Ui 的共用元件與核心的介面服務，所以連結介面的 QML 模組；用 offscreen 平台跑，
# 不需要螢幕。結果寫到測試暫存的檔案（視窗程式的輸出 ctest 收不到），失敗時看那個檔案
find_package(Qt6 REQUIRED COMPONENTS QuickTest)
add_executable(auralis_pl_quick_tests
    pl_quick_test_main.cpp
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ../../utf8.manifest
)
target_link_libraries(auralis_pl_quick_tests PRIVATE auralis_core auralis_ui auralis_uiplugin
    auralis_mod_playlists_qml auralis_mod_playlists_qmlplugin Qt6::QuickTest)
target_include_directories(auralis_pl_quick_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/modules/playlists
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/tests/ui)
target_compile_definitions(auralis_pl_quick_tests PRIVATE
    AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
file(MAKE_DIRECTORY "${AURALIS_TEST_TMP}/t-50-quick-result")
add_test(NAME PL.quick_sidebar
    COMMAND auralis_pl_quick_tests -o "${AURALIS_TEST_TMP}/t-50-quick-result/result.txt,txt")
set_tests_properties(PL.quick_sidebar PROPERTIES
    LABELS PL
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${AURALIS_QT_BIN}")
