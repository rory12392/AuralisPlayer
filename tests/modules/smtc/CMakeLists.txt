# ---- 系統媒體控制模組的測試（標籤 SMTC）----
add_executable(auralis_smtc_tests
    test_smtc_mapping.cpp
    # 真的向 Windows 取得 SMTC（隱藏的測試視窗）：註冊、狀態、結束清除、錯誤邊界、停用
    test_smtc_session.cpp
    # 曲目資訊的組成、晚到結果的比對、縮圖與程式圖示（T-81，假的音樂庫與封面服務）
    test_smtc_track.cpp
    # 假的播放服務（按鈕呼叫了哪個播放控制）
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    ../../utf8.manifest
)
# 模組是 OBJECT library：連結它就把物件檔（含靜態註冊）帶進來；測試直接 include 模組的標頭
target_link_libraries(auralis_smtc_tests PRIVATE auralis_core auralis_mod_smtc
    Catch2::Catch2WithMain WindowsApp Qt6::Gui)
target_include_directories(auralis_smtc_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/modules/smtc
    ${PROJECT_SOURCE_DIR}/tests/ui)
target_compile_definitions(auralis_smtc_tests PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}")
catch_discover_tests(auralis_smtc_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS SMTC
)
