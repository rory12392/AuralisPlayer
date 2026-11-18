find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)

# 測試只寫 .localdata\tmp\ 底下，不碰使用者的真實資料
set(AURALIS_TEST_TMP "${PROJECT_SOURCE_DIR}/.localdata/tmp/ctest")
file(MAKE_DIRECTORY "${AURALIS_TEST_TMP}")
set(AURALIS_QT_BIN "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")
set(AURALIS_TEST_ENV
    "PATH=path_list_prepend:${AURALIS_QT_BIN}"
    "AURALIS_TEST_TMP=set:${AURALIS_TEST_TMP}"
    "QT_QPA_PLATFORM=set:offscreen"
    "PYTHONUTF8=set:1"
)

# ctest 行程內先把 PATH 裡含非 ASCII 字元的項目移掉：Catch2 的測試探索（DL_PATHS）在 ctest 行程內
# 用 cmake_path(CONVERT) 處理 PATH，CMake 3.29 遇到這種項目會崩潰（0xC0000409）。
# 要排在所有 catch_discover_tests 之前，ctest 才會先執行它
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ctest_path_filter.cmake" @ONLY CONTENT [==[
# 由 tests/CMakeLists.txt 產生：移掉 PATH 裡含非 ASCII 字元的項目（只影響這次 ctest 與它啟動的測試）
set(_auralis_entries "$ENV{PATH}")
set(_auralis_kept "")
foreach(_auralis_entry IN LISTS _auralis_entries)
    string(REGEX REPLACE "[ -~]" "" _auralis_other "${_auralis_entry}")
    if(_auralis_other STREQUAL "")
        list(APPEND _auralis_kept "${_auralis_entry}")
    endif()
endforeach()
set(ENV{PATH} "${_auralis_kept}")
]==])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/ctest_path_filter.cmake")

# ---- 核心單元測試（標籤 core）----
add_executable(auralis_core_tests
    core/test_data_dir.cpp
    core/test_hires_deps.cpp
    core/test_job_runner.cpp
    core/test_module_settings.cpp
    core/test_output_settings.cpp
    core/test_result.cpp
    core/test_ui_services.cpp
    core/audio/engine_helpers.h
    core/audio/fake_wasapi.cpp
    core/audio/fake_wasapi.h
    core/audio/signal_writer.cpp
    core/audio/signal_writer.h
    core/audio/test_bitperfect.cpp
    core/audio/test_dsd_silence.cpp
    core/audio/test_dsd_drain.cpp
    core/audio/test_engine_seek.cpp
    core/audio/test_ffmpeg_open_errors.cpp
    core/audio/test_ffmpeg_protocol.cpp
    core/audio/test_ffmpeg_source.cpp
    core/audio/test_gapless.cpp
    core/audio/test_gapless_lossy.cpp
    core/audio/test_output_registry.cpp
    core/audio/test_processor_chain.cpp
    core/audio/test_realtime_rules.cpp
    core/audio/test_ring_buffer.cpp
    core/audio/test_seek_accuracy.cpp
    core/audio/test_soak.cpp
    core/audio/test_source_registry.cpp
    core/audio/test_volume.cpp
    core/audio/test_volume_live.cpp
    core/audio/volume_probe.h
    core/audio/test_wasapi_backend.cpp
    core/covers/test_cover_image.cpp
    core/covers/test_cover_service.cpp
    core/library/cue_fixtures.cpp
    core/library/cue_fixtures.h
    core/library/library_fixtures.cpp
    core/library/library_fixtures.h
    core/library/test_content_path.cpp
    core/library/test_cue_library.cpp
    core/library/test_cue_open_source.cpp
    core/library/test_cue_playback.cpp
    core/library/test_cue_sheet.cpp
    core/library/test_drag_query.cpp
    core/library/test_embedded_sylt.cpp
    core/library/test_library_input_hardening.cpp
    core/library/test_library_scan.cpp
    core/library/test_library_service.cpp
    core/library/test_library_watcher.cpp
    core/library/test_migrations.cpp
    core/library/test_open_paths.cpp
    core/library/test_scan_links.cpp
    core/library/test_tag_reader.cpp
    core/library/test_text_decoder.cpp
    core/playback/test_dsd_output_path.cpp
    core/playback/test_dsd_pcm_volume.cpp
    core/playback/test_output_status.cpp
    core/playback/test_playback_cue.cpp
    core/playback/test_playback_queue.cpp
    core/playback/test_playback_service.cpp
    core/playback/test_stop_release.cpp
    core/playback/test_volume_service.cpp
    # 外掛管理（T-103）：用下面建的假外掛 DLL
    core/plugins/plugin_fixtures.h
    core/plugins/test_plugin_builtin.cpp
    core/plugins/test_plugin_loading.cpp
    core/plugins/test_plugin_manage.cpp
    # 主程式的長時間播放欠載檢查（--soak）：判定與 JSON 在核心測試裡直接測
    ${PROJECT_SOURCE_DIR}/src/app/soak.cpp
    utf8.manifest
)
target_link_libraries(auralis_core_tests PRIVATE auralis_core Catch2::Catch2WithMain Qt6::Test)
target_include_directories(auralis_core_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/app)
# 封面測試在測試時產生圖檔（QImage），並直接呼叫 image://cover 影像提供者
target_link_libraries(auralis_core_tests PRIVATE Qt6::Gui Qt6::Quick)
target_include_directories(auralis_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/library)
# 測試音樂庫的標籤用 TagLib 寫進測試時產生的檔案
find_package(TagLib CONFIG REQUIRED)
target_link_libraries(auralis_core_tests PRIVATE TagLib::tag)
target_compile_definitions(auralis_core_tests PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    AURALIS_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
# 測試時用 FFmpeg 的編碼器產生測試訊號
find_package(FFMPEG REQUIRED)
target_include_directories(auralis_core_tests PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(auralis_core_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(auralis_core_tests PRIVATE ${FFMPEG_LIBRARIES})
# 進階輸出的建置依賴只做連結與編譯的確認；ASIO SDK 只在 AURALIS_WITH_ASIO=ON 時有
target_link_libraries(auralis_core_tests PRIVATE auralis::ebur128)
if(TARGET auralis::asiosdk)
    target_link_libraries(auralis_core_tests PRIVATE auralis::asiosdk)
endif()
# 假外掛 DLL（T-103）：同一份原始碼建成四種行為（正常、丟例外、回傳空值、ABI 不同），只用 C 介面
set(fake_plugin_dir "${CMAKE_CURRENT_BINARY_DIR}/fake_plugins")
foreach(mode ok throw null abi)
    add_library(auralis_fake_plugin_${mode} SHARED core/plugins/fake_plugin.cpp)
    target_include_directories(auralis_fake_plugin_${mode} PRIVATE ${PROJECT_SOURCE_DIR}/src/core/api)
    target_compile_definitions(auralis_fake_plugin_${mode} PRIVATE
        FAKE_PLUGIN_MODE="${mode}" FAKE_PLUGIN_ID="fake-${mode}")
    set_target_properties(auralis_fake_plugin_${mode} PROPERTIES
        OUTPUT_NAME fake_plugin_${mode} RUNTIME_OUTPUT_DIRECTORY "${fake_plugin_dir}")
    add_dependencies(auralis_core_tests auralis_fake_plugin_${mode})
endforeach()
target_compile_definitions(auralis_core_tests PRIVATE AURALIS_FAKE_PLUGINS="${fake_plugin_dir}")
catch_discover_tests(auralis_core_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS core
)

# 主程式的自我檢查與資料夾保護（標籤 core）
add_test(NAME app.self_check
    COMMAND AuralisPlayer --self-check --data-dir "${AURALIS_TEST_TMP}/self-check")
add_test(NAME app.self_check_requires_data_dir COMMAND AuralisPlayer --self-check)
set_tests_properties(app.self_check_requires_data_dir PROPERTIES WILL_FAIL TRUE)
# 長時間播放欠載的短版（里程碑完整驗證跑 --soak 60）：欠載、沒換過格式、播放失敗都讓結束碼不是 0
add_test(NAME app.self_check_soak
    COMMAND AuralisPlayer --self-check --data-dir "${AURALIS_TEST_TMP}/self-check-soak" --soak 5)
# 以實際時間的裝置節奏數欠載：不和其他測試同時跑，別的測試佔滿 CPU 時會有假的欠載
set_tests_properties(app.self_check_soak PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
# 同上加 DSD64（--soak-dsd）：經進階輸出模組註冊的包裝器；沒有註冊時 DSD 部分略過、結束碼仍是 0
add_test(NAME app.self_check_soak_dsd
    COMMAND AuralisPlayer --self-check --data-dir "${AURALIS_TEST_TMP}/self-check-soak-dsd"
        --soak 5 --soak-dsd)
set_tests_properties(app.self_check_soak_dsd PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
# 同上但全程開著全螢幕頁（--fullscreen，T-76；全螢幕頁由歌詞模組登記，沒有時結束碼 1）
add_test(NAME app.self_check_soak_fullscreen
    COMMAND AuralisPlayer --self-check --data-dir "${AURALIS_TEST_TMP}/self-check-soak-fullscreen"
        --soak 5 --fullscreen)
set_tests_properties(app.self_check_soak_fullscreen PROPERTIES TIMEOUT 60 RUN_SERIAL TRUE)
# 自我檢查印出 FFmpeg 的版本與建置設定，發佈前的授權檢查照它判斷（T-98，STACK §10）
add_test(NAME app.self_check_ffmpeg
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/core/check_self_check_ffmpeg.py
            $<TARGET_FILE:AuralisPlayer> "${AURALIS_TEST_TMP}/self-check-ffmpeg")
# 發佈時留存對應原始碼的腳本：乾跑列得出每個 port 的原始碼封存與 Qt 模組，且不寫檔（T-102，STACK §9）
add_test(NAME app.collect_sources_dry_run
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/core/check_collect_sources.py
            ${PROJECT_SOURCE_DIR} ${CMAKE_BINARY_DIR} "${AURALIS_TEST_TMP}/collect-sources")
set_tests_properties(app.collect_sources_dry_run PROPERTIES SKIP_RETURN_CODE 77)
# 自我檢查列出外掛與狀態；外掛壞掉不讓自我檢查失敗（T-103）
add_test(NAME app.self_check_plugins
    COMMAND Python3::Interpreter ${CMAKE_CURRENT_SOURCE_DIR}/core/check_self_check_plugins.py
            $<TARGET_FILE:AuralisPlayer> "${AURALIS_TEST_TMP}/self-check-plugins")
set(app_tests app.self_check app.self_check_requires_data_dir app.self_check_soak
    app.self_check_soak_dsd app.self_check_soak_fullscreen app.self_check_ffmpeg
    app.collect_sources_dry_run app.self_check_plugins)
if(AURALIS_DEV_BUILD)
    # 開發版沒有資料夾時要拒絕；LOCALAPPDATA 也指到暫存，保護失效時才不會寫進真實資料夾
    add_test(NAME app.dev_build_refuses_without_data_dir COMMAND AuralisPlayer)
    set_tests_properties(app.dev_build_refuses_without_data_dir PROPERTIES
        PASS_REGULAR_EXPRESSION "AURALIS_DATA_DIR"
        ENVIRONMENT_MODIFICATION "AURALIS_DATA_DIR=unset:;LOCALAPPDATA=set:${AURALIS_TEST_TMP}/fake-localappdata"
        TIMEOUT 20)
    list(APPEND app_tests app.dev_build_refuses_without_data_dir)
endif()
set_property(TEST ${app_tests} APPEND PROPERTY LABELS core)
set_property(TEST ${app_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION "${AURALIS_TEST_ENV}")

# ---- 效能量測（標籤 perf；STACK §6：ctest --preset windows-release -L perf）----
add_executable(auralis_perf_tests
    perf/test_scan_perf.cpp
    core/audio/signal_writer.cpp
    core/library/library_fixtures.cpp
    utf8.manifest
)
target_link_libraries(auralis_perf_tests PRIVATE auralis_core Catch2::Catch2WithMain TagLib::tag)
target_include_directories(auralis_perf_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/core/library
    ${FFMPEG_INCLUDE_DIRS})
target_link_directories(auralis_perf_tests PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(auralis_perf_tests PRIVATE ${FFMPEG_LIBRARIES})
target_compile_definitions(auralis_perf_tests PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}")
catch_discover_tests(auralis_perf_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    # 量時間的測試不和其他測試同時跑
    PROPERTIES LABELS perf TIMEOUT 3600 RUN_SERIAL TRUE
)

# ---- 介面測試（標籤 ui）：主題與主視窗狀態、共用元件的鍵盤與焦點、截圖與開頁 ----
add_executable(auralis_ui_tests
    ui/catch_gui_main.cpp
    ui/fake_outputs.h
    ui/fake_playback_service.h
    ui/test_dialogs.cpp
    ui/test_icon_library.cpp
    ui/test_library_controller.cpp
    ui/test_now_playing.cpp
    ui/test_open_files.cpp
    ui/test_output_controller.cpp
    ui/test_queue_drag_query.cpp
    ui/test_queue_model.cpp
    ui/test_single_instance.cpp
    ui/test_theme_controller.cpp
    # 主程式的單一執行個體（AC-11-5）不在函式庫裡，原始檔直接編進來測
    ${PROJECT_SOURCE_DIR}/src/app/single_instance.cpp
    utf8.manifest
)
# 外部檔案拖放的測試（test_open_files）載入 Auralis.Ui 的 QML，所以連結 QML 外掛
target_link_libraries(auralis_ui_tests PRIVATE auralis_ui auralis_uiplugin Catch2::Catch2 Qt6::Test)
target_compile_definitions(auralis_ui_tests PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    AURALIS_ICON_DIR="${PROJECT_SOURCE_DIR}/src/ui/icons")
target_include_directories(auralis_ui_tests PRIVATE ${PROJECT_SOURCE_DIR}/src/ui
    ${PROJECT_SOURCE_DIR}/src/app)
catch_discover_tests(auralis_ui_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    # 系統深淺色的訊號要用真的 Windows 平台外掛（offscreen 不會回報），測試本身不開視窗
    PROPERTIES LABELS ui ENVIRONMENT "QT_QPA_PLATFORM=windows"
)

add_executable(auralis_quick_tests ui/quick_test_main.cpp ui/fake_outputs.h
    ui/fake_playback_service.h ui/frame_probe.h ui/fake_plugins.h)
target_link_libraries(auralis_quick_tests PRIVATE auralis_ui auralis_uiplugin Qt6::QuickTest)
target_compile_definitions(auralis_quick_tests PRIVATE
    AURALIS_TEST_TMP="${AURALIS_TEST_TMP}"
    QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/ui/qml")
# Qt Quick Test 是視窗程式，ctest 收不到它的標準輸出，失敗時原本什麼訊息都沒有：
# 結果用 -o 寫到測試暫存的檔案，失敗（含逾時）時由這支腳本印出來，看得到是哪個函式
set(quick_tests_out "${AURALIS_TEST_TMP}/quick-tests/result.txt")
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/run_quick_tests.cmake" @ONLY CONTENT [==[
# 由 tests/CMakeLists.txt 產生：cmake -DEXE=<auralis_quick_tests> -DOUT=<輸出檔> -P run_quick_tests.cmake
get_filename_component(_auralis_out_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${_auralis_out_dir}")
file(REMOVE "${OUT}")
# 逾時比 ctest 的 TIMEOUT 短：卡住時由這裡結束它，才來得及把已經寫出的部分印出來
execute_process(COMMAND "${EXE}" -o "${OUT},txt" RESULT_VARIABLE _auralis_rc TIMEOUT 150)
if(NOT _auralis_rc STREQUAL "0")
    if(EXISTS "${OUT}")
        file(READ "${OUT}" _auralis_text)
        message("${_auralis_text}")
    endif()
    message(FATAL_ERROR "ui.quick_tests 失敗（${_auralis_rc}）；完整輸出在 ${OUT}")
endif()
]==])
add_test(NAME ui.quick_tests
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:auralis_quick_tests> -DOUT=${quick_tests_out}
            -P "${CMAKE_CURRENT_BINARY_DIR}/run_quick_tests.cmake")
set_tests_properties(ui.quick_tests PROPERTIES TIMEOUT 180)
# 進出全螢幕不畫出跑版的一格（P-LYR-11）：要真的 windows 平台視窗，會在螢幕上開最上層的視窗並進出
# 全螢幕約一兩秒，所以不放進 -L ui（使用者常在前景用電腦），標籤 window，任務檢查與里程碑驗證另外跑
add_test(NAME window.fullscreen_transition
    COMMAND auralis_quick_tests -input "${CMAKE_CURRENT_SOURCE_DIR}/ui/qml/tst_fullscreen_transition.qml")
set_tests_properties(window.fullscreen_transition PROPERTIES TIMEOUT 120 LABELS window
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${AURALIS_QT_BIN};QT_QPA_PLATFORM=set:windows")

set(screenshot_dir "${AURALIS_TEST_TMP}/screenshots")
add_test(NAME ui.screenshot_shell
    COMMAND AuralisPlayer --data-dir "${AURALIS_TEST_TMP}/ui-app" --screenshot shell
            --size 960x640 --theme paper --out "${screenshot_dir}/shell.png")
set_tests_properties(ui.screenshot_shell PROPERTIES PASS_REGULAR_EXPRESSION "截圖：")
add_test(NAME ui.page_unknown
    COMMAND AuralisPlayer --data-dir "${AURALIS_TEST_TMP}/ui-app-page" --page no/such-page)
set_tests_properties(ui.page_unknown PROPERTIES PASS_REGULAR_EXPRESSION "沒有這個頁面")
add_test(NAME ui.dev_page
    COMMAND AuralisPlayer --data-dir "${AURALIS_TEST_TMP}/ui-app-dev" --screenshot dev/ui
            --size 1600x900 --theme void --out "${screenshot_dir}/dev-ui.png")
# 頁面 id 可以帶開頁參數（展示頁的區段，截圖用）
add_test(NAME ui.dev_page_section
    COMMAND AuralisPlayer --data-dir "${AURALIS_TEST_TMP}/ui-app-dev-section" --screenshot "dev/ui?section=lists"
            --size 960x640 --theme paper --out "${screenshot_dir}/dev-ui-lists.png")
# 元件展示頁只在開發建置；發佈建置要回報沒有這個頁面（介面規範 §8）
if(AURALIS_DEV_UI)
    set_tests_properties(ui.dev_page ui.dev_page_section PROPERTIES PASS_REGULAR_EXPRESSION "截圖：")
else()
    set_tests_properties(ui.dev_page ui.dev_page_section PROPERTIES PASS_REGULAR_EXPRESSION "沒有這個頁面")
endif()
# 命令列給檔案（AC-11-5）：選項與選項的值不能被當成檔案；加入佇列並開始播放（截圖模式用 Capture 後端，
# 不開實體裝置）；網址照「只開啟本機檔案」略過
add_test(NAME ui.command_line_open
    COMMAND AuralisPlayer --data-dir "${AURALIS_TEST_TMP}/ui-app-open" --screenshot "shell?panel=queue"
            --size 960x640 --theme paper --out "${screenshot_dir}/open.png"
            "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audio/sine-440hz-1s.mp3" "http://auralis-test.invalid/a.flac")
set_tests_properties(ui.command_line_open PROPERTIES
    PASS_REGULAR_EXPRESSION "開啟：加入 1 首，播放中，目前曲目：.*sine-440hz-1s\\.mp3.*截圖：")
# 截圖的 &panel=<面板 id 後半>：切到那個面板頁籤（收合的面板先展開）；對不到已登記的面板時失敗、
# stderr 列出已登記的面板 id，不靜默截成預設頁籤
add_test(NAME ui.screenshot_panel
    COMMAND AuralisPlayer --data-dir "${AURALIS_TEST_TMP}/ui-app-panel" --screenshot "shell?panel=now-playing"
            --size 960x640 --theme paper --out "${screenshot_dir}/panel-now-playing.png")
set_tests_properties(ui.screenshot_panel PROPERTIES
    PASS_REGULAR_EXPRESSION "面板：panel/now-playing.*截圖：")
add_test(NAME ui.screenshot_panel_unknown
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:AuralisPlayer>
            "-DARGS=--data-dir|${AURALIS_TEST_TMP}/ui-app-panel-unknown|--screenshot|shell?panel=no-such|--size|960x640|--out|${screenshot_dir}/panel-unknown.png"
            "-DSTDERR_REGEX=沒有這個面板：panel/no-such.*panel/now-playing.*panel/queue"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/ui/expect_failure.cmake")
set(ui_script_tests ui.quick_tests ui.screenshot_shell ui.page_unknown ui.dev_page ui.dev_page_section
    ui.command_line_open ui.screenshot_panel ui.screenshot_panel_unknown)
set_property(TEST ${ui_script_tests} APPEND PROPERTY LABELS ui)
set_property(TEST ${ui_script_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION "${AURALIS_TEST_ENV}")

# ---- 隔離測試（標籤 boundary）：停用、故障注入、include 掃描、能力合約、單檔行數 ----
add_executable(auralis_boundary_tests
    boundary/fake_modules.cpp
    boundary/fake_modules.h
    boundary/test_module_isolation.cpp
    boundary/test_write_whitelist.cpp
    # 模組停止時等自己的背景工作（T-97）：直接用各模組送背景工作的元件與假服務
    boundary/stop_fakes.h
    boundary/test_module_stop.cpp
    utf8.manifest
)
target_link_libraries(auralis_boundary_tests PRIVATE auralis_core Catch2::Catch2WithMain)
target_compile_definitions(auralis_boundary_tests PRIVATE AURALIS_TEST_TMP="${AURALIS_TEST_TMP}")
auralis_link_modules(auralis_boundary_tests)
target_link_libraries(auralis_boundary_tests PRIVATE auralis_mod_playlists_qml
    auralis_mod_lyrics_qml auralis_mod_hires_output_qml)
target_include_directories(auralis_boundary_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/core
    ${PROJECT_SOURCE_DIR}/src/modules/playlists
    ${PROJECT_SOURCE_DIR}/src/modules/lyrics
    ${PROJECT_SOURCE_DIR}/src/modules/hires_output
    ${PROJECT_SOURCE_DIR}/src/modules/smtc)
# Catch2 測試的 PATH 由 DL_PATHS 帶入；其他設定編進程式（PROPERTIES 不能傳含分號的清單）
catch_discover_tests(auralis_boundary_tests
    DISCOVERY_MODE PRE_TEST
    DL_PATHS "${AURALIS_QT_BIN}"
    PROPERTIES LABELS boundary
)

set(boundary_dir "${CMAKE_CURRENT_SOURCE_DIR}/boundary")
add_test(NAME boundary.file_length
    COMMAND ${CMAKE_COMMAND} -DROOT=${PROJECT_SOURCE_DIR} -P ${boundary_dir}/check_file_length.cmake)
add_test(NAME boundary.file_length_self_test
    COMMAND ${CMAKE_COMMAND} -DSELF_TEST=ON -DWORK_DIR=${AURALIS_TEST_TMP}
            -P ${boundary_dir}/check_file_length.cmake)
add_test(NAME boundary.includes
    COMMAND Python3::Interpreter ${boundary_dir}/check_includes.py --root ${PROJECT_SOURCE_DIR})
add_test(NAME boundary.includes_self_test
    COMMAND Python3::Interpreter ${boundary_dir}/check_includes.py --self-test)
add_test(NAME boundary.capabilities
    COMMAND Python3::Interpreter ${boundary_dir}/check_capabilities.py --root ${PROJECT_SOURCE_DIR})
add_test(NAME boundary.capabilities_self_test
    COMMAND Python3::Interpreter ${boundary_dir}/check_capabilities.py --self-test)
# 對外連線只有 LRCLIB：歌詞模組以外不得 include Qt Network（T-78）
add_test(NAME boundary.network_includes
    COMMAND Python3::Interpreter ${boundary_dir}/check_network.py --root ${PROJECT_SOURCE_DIR})
add_test(NAME boundary.network_includes_self_test
    COMMAND Python3::Interpreter ${boundary_dir}/check_network.py --self-test)
# 模組送進 JobRunner 的工作都要用停止閘門（JobGate，T-97）：submit 次數 ≤ Pass 次數、有 close
add_test(NAME boundary.job_gate
    COMMAND Python3::Interpreter ${boundary_dir}/check_job_gate.py --root ${PROJECT_SOURCE_DIR})
add_test(NAME boundary.job_gate_self_test
    COMMAND Python3::Interpreter ${boundary_dir}/check_job_gate.py --self-test)
# 連結守門的自我測試：用假專案只跑設定，歌詞模組連結 Network 通過、核心或其他模組連結時設定失敗
foreach(case lyrics core smtc)
    add_test(NAME boundary.network_guard_${case}
        COMMAND ${CMAKE_COMMAND} --fresh -S ${boundary_dir}/network_guard
                -B ${CMAKE_CURRENT_BINARY_DIR}/network_guard/${case} -G ${CMAKE_GENERATOR}
                -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM} -DFIXTURE_CASE=${case})
endforeach()
set_tests_properties(boundary.network_guard_lyrics PROPERTIES PASS_REGULAR_EXPRESSION "網路守門通過")
set_tests_properties(boundary.network_guard_core PROPERTIES PASS_REGULAR_EXPRESSION "違反：fake_core")
set_tests_properties(boundary.network_guard_smtc PROPERTIES PASS_REGULAR_EXPRESSION "違反：fake_mod_smtc")
# 主程式不含 ASIO（T-103）：主程式、核心與每個模組的連結清單沒有 ASIO SDK 與外掛；exe 裡沒有 ASIO 的程式。
# 建置了 ASIO 外掛時，用外掛當對照組確認兩個檢查都抓得到
get_property(asio_check_targets GLOBAL PROPERTY AURALIS_MODULE_TARGETS)
list(APPEND asio_check_targets AuralisPlayer auralis_core auralis_ui auralis_mod_hires_output_qml)
set(asio_links_content "")
foreach(target IN LISTS asio_check_targets)
    string(APPEND asio_links_content "${target}|$<TARGET_PROPERTY:${target},LINK_LIBRARIES>\n")
endforeach()
if(TARGET auralis_asio_impl)
    string(APPEND asio_links_content
        "CONTROL auralis_asio_impl|$<TARGET_PROPERTY:auralis_asio_impl,LINK_LIBRARIES>\n")
endif()
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/asio_links.txt" CONTENT "${asio_links_content}")
add_test(NAME boundary.no_asio_sdk_links
    COMMAND ${CMAKE_COMMAND} -DLINKS_FILE=${CMAKE_CURRENT_BINARY_DIR}/asio_links.txt
            -P ${boundary_dir}/check_asio_links.cmake)
add_test(NAME boundary.no_asio_sdk_strings
    COMMAND ${CMAKE_COMMAND} -DSCAN_FILE=$<TARGET_FILE:AuralisPlayer> -DEXPECT=absent
            -P ${PROJECT_SOURCE_DIR}/cmake/AuralisNoAsioCheck.cmake)
# 主程式包的打包後檢查：內建 ASIO 外掛少了外掛、標誌或授權時失敗（T-105）
add_test(NAME boundary.bundled_asio_package_check
    COMMAND Python3::Interpreter ${boundary_dir}/check_bundled_asio.py ${CMAKE_COMMAND}
            ${PROJECT_SOURCE_DIR}/cmake/AuralisNoAsioCheck.cmake "${AURALIS_TEST_TMP}/bundled-asio")
set(asio_boundary_tests boundary.no_asio_sdk_links boundary.no_asio_sdk_strings
    boundary.bundled_asio_package_check)
if(TARGET auralis_plugin_asio)
    add_test(NAME boundary.asio_strings_control
        COMMAND ${CMAKE_COMMAND} -DSCAN_FILE=$<TARGET_FILE:auralis_plugin_asio> -DEXPECT=present
                -P ${PROJECT_SOURCE_DIR}/cmake/AuralisNoAsioCheck.cmake)
    list(APPEND asio_boundary_tests boundary.asio_strings_control)
endif()
set(script_tests ${asio_boundary_tests} boundary.file_length boundary.file_length_self_test boundary.includes
    boundary.includes_self_test boundary.capabilities boundary.capabilities_self_test
    boundary.network_includes boundary.network_includes_self_test
    boundary.job_gate boundary.job_gate_self_test
    boundary.network_guard_lyrics boundary.network_guard_core boundary.network_guard_smtc)
set_property(TEST ${script_tests} APPEND PROPERTY LABELS boundary)
set_property(TEST ${script_tests} APPEND PROPERTY ENVIRONMENT_MODIFICATION "${AURALIS_TEST_ENV}")

# ---- 以實際時間為門檻的 Catch2 測試不和其他測試同時跑（RUN_SERIAL）----
# 8 個測試平行時 CPU 被佔滿，「UI 執行緒最長間隔 < 200 ms」這類門檻會偶發超過。Catch2 的測試在 ctest
# 執行時才探索出來，這裡設定不到，所以產生一支排在探索之後的腳本，照測試名稱設定。
# 新增以實際時間為門檻的測試時把名稱加進來；名稱對不到（改名、刪掉）時 ctest 開頭會警告
file(CONFIGURE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ctest_serial_tests.cmake" @ONLY CONTENT [==[
# 由 tests/CMakeLists.txt 產生
set(_auralis_timing_tests
    [=[AC-08-1：加入資料夾後在背景掃描，UI 執行緒照常運作並顯示已掃描數／總數]=]
    [=[AC-08-2：重新啟動後在背景對已加入的資料夾做增量檢查，沒有變動時不改資料]=]
    [=[AC-08-3：資料夾中新增、刪除、移動檔案後，音樂庫在 10 秒內更新]=]
    [=[AC-08-3：連續的變動去抖 2 秒後只掃描一次；掃描本身不會觸發監看]=]
    [=[AC-19-1：跟隨系統時，系統深淺色改變後 1 秒內跟著變]=]
    [=[AC-02-7：播放中改音量不重開裝置、不掉樣本，增益連續過渡，回到 100% 後恢復逐位元相同]=]
    [=[AC-02-7：同格式接續的前後改音量，兩首之間不掉樣本、增益連續]=]
    [=[AC-02-7：改音量到送進裝置的延遲在 0.2 秒內（FLAC 經 FFmpeg 音源，不重開裝置）]=]
    [=[AC-04-5：soak 經 DSD 包裝器播 DSD64，和 PCM 交替，沒有欠載與不連續]=]
)
set(_auralis_known ${auralis_core_tests_TESTS} ${auralis_ui_tests_TESTS})
foreach(_auralis_test IN LISTS _auralis_timing_tests)
    # ctest 讀這支腳本時沒有啟用 if(IN_LIST) 的政策，用 list(FIND)
    list(FIND _auralis_known "${_auralis_test}" _auralis_index)
    if(NOT _auralis_index EQUAL -1)
        set_tests_properties("${_auralis_test}" PROPERTIES RUN_SERIAL TRUE)
    elseif(_auralis_known)
        message(WARNING "計時類測試的名稱對不到，沒有設成 RUN_SERIAL（tests/CMakeLists.txt）：${_auralis_test}")
    endif()
endforeach()
]==])
set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${CMAKE_CURRENT_BINARY_DIR}/ctest_serial_tests.cmake")
add_subdirectory(modules/hires_output)
add_subdirectory(modules/library_browse)
# 多片專輯的 Disc 子資料夾分組（T-91）
target_sources(auralis_core_tests PRIVATE core/library/test_disc_folders.cpp)
add_subdirectory(modules/playlists)
add_subdirectory(modules/lyrics)
add_subdirectory(modules/smtc)
