# ---- 歌詞與沉浸模組的測試（標籤 LYR）----
# 快取用核心真正的 ModuleDatabase（升級到最新 schema 的測試資料庫）；內嵌歌詞的實測在測試時產生 FLAC
# 再經核心的 readEmbeddedLyrics 讀（共用核心測試的產生器），所以連結 auralis_core、TagLib、FFmpeg
find_package(TagLib CONFIG REQUIRED)
find_package(FFMPEG REQUIRED)

add_executable(auralis_lyr_tests
    lyr_test_helpers.h
    test_lrc_parser.cpp
    test_lyrics_sources.cpp
    test_lyrics_cache.cpp
    test_lyrics_loader.cpp
    # 歌詞面板的資料（單例 Lyrics，T-66）：用介面測試的假播放服務
    test_lyrics_view.cpp
    # LRCLIB（T-67）：對本機的假伺服器（QTcpServer），不打真的 LRCLIB
    fake_lrclib_server.h
    test_lrclib.cpp
    test_lrclib_loader.cpp
    # LRCLIB 限流（T-99）：429／503 照 Retry-After 冷卻
    test_lrclib_rate_limit.cpp
    # 全螢幕底色（T-69）：主色壓暗、背景取色、快速切歌
    test_backdrop.cpp
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ${PROJECT_SOURCE_DIR}/tests/core/library/library_fixtures.cpp
    ${PROJECT_SOURCE_DIR}/tests/core/audio/signal_writer.cpp
    ../../utf8.manifest
)
# 模組是 OBJECT library：連結它就把物件檔（含靜態註冊）帶進來；解析、載入器與單例在 QML 模組的
# 靜態函式庫裡（T-66 搬過去），測試直接 include 模組的標頭
target_link_libraries(auralis_lyr_tests PRIVATE auralis_core auralis_mod_lyrics
    auralis_mod_lyrics_qml Catch2::Catch2WithMain TagLib::tag Qt6::Network)
target_include_directories(auralis_lyr_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/modules/lyrics
    ${PROJECT_SOURCE_DIR}/tests/core/library
    ${PROJECT_SOURCE_DIR}/tests/core/audio
    ${PROJECT_SOURCE_DIR}/tests/ui
    ${FFMPEG_INCLUDE_DIRS})
target_link_directories(auralis_lyr_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(auralis_lyr_tests PRIVATE ${FFMPEG_LIBRARIES})
target_compile_definitions(auralis_lyr_tests PRIVATE
    AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    AURALIS_LYR_SAMPLES="${CMAKE_CURRENT_SOURCE_DIR}/samples")
catch_discover_tests(auralis_lyr_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS LYR
)

# 歌詞面板與預覽的 Qt Quick Test（qml/tst_*.qml，T-66）。面板用到 Auralis.Ui 的共用元件與核心的
# 介面服務，所以連結介面的 QML 模組；用 offscreen 平台跑，不需要螢幕。結果寫到測試暫存的檔案
# （視窗程式的輸出 ctest 收不到），失敗時看那個檔案
find_package(Qt6 REQUIRED COMPONENTS QuickTest)
add_executable(auralis_lyr_quick_tests
    lyr_quick_test_main.cpp
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ../../utf8.manifest
)
target_link_libraries(auralis_lyr_quick_tests PRIVATE auralis_core auralis_ui auralis_uiplugin
    auralis_mod_lyrics_qml auralis_mod_lyrics_qmlplugin Qt6::QuickTest)
target_include_directories(auralis_lyr_quick_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/modules/lyrics
    ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/tests/ui)
target_compile_definitions(auralis_lyr_quick_tests PRIVATE
    AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/qml")
file(MAKE_DIRECTORY "${AURALIS_TEST_TMP}/t-66-quick-result")
add_test(NAME LYR.quick_panels
    COMMAND auralis_lyr_quick_tests -o "${AURALIS_TEST_TMP}/t-66-quick-result/result.txt,txt")
set_tests_properties(LYR.quick_panels PROPERTIES
    LABELS LYR
    TIMEOUT 120
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${AURALIS_QT_BIN}")
