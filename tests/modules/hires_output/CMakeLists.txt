# ---- 進階輸出模組的測試（標籤 OUT）----
add_executable(auralis_out_tests
    dsd_fixtures.cpp
    dsd_fixtures.h
    dsd_test_helpers.h
    test_dsd_hardening.cpp
    dop_test_helpers.h
    test_dop_sink.cpp
    test_dop_switch.cpp
    test_dsd_source.cpp
    test_native_dsd.cpp
    dsd_tone.h
    test_pcm_convert.cpp
    test_out_module.cpp
    test_dsd_mode.cpp
    test_replay_gain.cpp
    test_rg_engine.cpp
    test_dsd_path_change.cpp
    test_r128.cpp
    # R128 計算 CUE 整軌：測試時用 FFmpeg 把訊號編成 FLAC（共用核心測試的產生器）
    test_r128_cue.cpp
    # 輸出面板與設定頁的選項同步：載入模組真正的 QML 區塊（要共用元件的 QML 模組與主題）
    test_section_sync.cpp
    # 曲目右鍵選單的「計算 ReplayGain」（T-88）：載入模組真正的區塊
    test_r128_track_menu.cpp
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_outputs.h
    ${PROJECT_SOURCE_DIR}/tests/core/audio/signal_writer.cpp
    # 假的播放服務（DSD 輸出方式在播放中切換時呼叫了哪些方法）
    ${PROJECT_SOURCE_DIR}/tests/ui/fake_playback_service.h
    # 假的 WASAPI 裝置（DoP 經核心的 WASAPI 獨占後端送到裝置的位元比對）
    ${PROJECT_SOURCE_DIR}/tests/core/audio/fake_wasapi.cpp
    ../../utf8.manifest
)
# 模組是 OBJECT library：連結它就把物件檔（含靜態註冊）帶進來；測試直接 include 模組的標頭
target_link_libraries(auralis_out_tests PRIVATE auralis_core auralis_mod_hires_output
    auralis_mod_hires_output_qml Catch2::Catch2WithMain Qt6::Gui)
target_include_directories(auralis_out_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/modules/hires_output
    ${PROJECT_SOURCE_DIR}/tests/core/audio
    ${PROJECT_SOURCE_DIR}/tests/ui)
# 「內嵌封面讀得到」的測試用 TagLib 把標籤與封面寫進測試時產生的檔案
find_package(TagLib CONFIG REQUIRED)
target_link_libraries(auralis_out_tests PRIVATE TagLib::tag)
target_compile_definitions(auralis_out_tests PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}")
target_link_libraries(auralis_out_tests PRIVATE auralis_ui auralis_uiplugin)
# signal_writer.cpp 直接用 FFmpeg 的編碼器
find_package(FFMPEG REQUIRED)
target_include_directories(auralis_out_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(auralis_out_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(auralis_out_tests PRIVATE ${FFMPEG_LIBRARIES})
# ASIO 後端的測試用假的驅動（實作 IASIO），只在 AURALIS_WITH_ASIO=ON 時建置。
# 後端在 ASIO 外掛（T-103），測試直接連外掛的後端本體；另外載入建好的 AuralisAsio.dll 走一次外掛管理
if(TARGET auralis_asio_impl)
    target_sources(auralis_out_tests PRIVATE fake_asio.h test_asio_backend.cpp test_asio_dsd.cpp
        test_asio_plugin.cpp)
    target_link_libraries(auralis_out_tests PRIVATE auralis_asio_impl)
    # IASIO 介面的宣告需要完整的 COM 標頭（interface 巨集）
    set_source_files_properties(test_asio_backend.cpp test_asio_dsd.cpp
        PROPERTIES COMPILE_OPTIONS /UWIN32_LEAN_AND_MEAN)
    add_dependencies(auralis_out_tests auralis_plugin_asio)
    target_compile_definitions(auralis_out_tests PRIVATE
        AURALIS_ASIO_PLUGIN_DLL="$<TARGET_FILE:auralis_plugin_asio>"
        AURALIS_TEST_APP_VERSION="${PROJECT_VERSION}")
endif()
catch_discover_tests(auralis_out_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS OUT
)
