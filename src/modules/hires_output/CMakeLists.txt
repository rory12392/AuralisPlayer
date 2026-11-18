# 進階輸出（OUT）：DSF／DFF 音源、DoP、ASIO PCM 與原生 DSD、轉 PCM、DSD 輸出方式的區塊；
# ReplayGain 處理器與區塊（T-37）；R128 計算（T-38）。
auralis_add_module(auralis_mod_hires_output
    SOURCES
        dff_parser.cpp
        dop_sink.cpp
        dop_sink.h
        dsd_file.cpp
        dsd_file.h
        dsd_source.cpp
        dsd_source.h
        dsd_stream.h
        dsf_parser.cpp
        hires_output_module.cpp
        native_dsd_sink.cpp
        native_dsd_sink.h
        pcm_convert_sink.cpp
        pcm_convert_sink.h
        replay_gain_processor.cpp
        replay_gain_processor.h
)
# 模組的標頭以模組資料夾為根（#include "dsd_source.h"）
target_include_directories(auralis_mod_hires_output PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 設定頁與輸出面板的區塊（T-36）：QML 模組 Auralis.Out（單例 DsdOutput 與兩個區塊）。
# 靜態的 QML 外掛，模組的 hires_output_module.cpp 用 Q_IMPORT_QML_PLUGIN 帶進來；
# 只依賴核心 API 與 Qt，包裝器由模組在 start() 交給它建立
qt_add_library(auralis_mod_hires_output_qml STATIC)
# 原始檔放 qml/（專案規範），資源放在模組根目錄：同一個目錄的 QML 才看得到模組自己的單例 DsdOutput
set_source_files_properties(qml/DsdPanelSection.qml PROPERTIES QT_RESOURCE_ALIAS DsdPanelSection.qml)
set_source_files_properties(qml/DsdSettingsSection.qml
    PROPERTIES QT_RESOURCE_ALIAS DsdSettingsSection.qml)
# ReplayGain 的兩個區塊（T-37，單例 ReplayGain）
set_source_files_properties(qml/ReplayGainPanelSection.qml
    PROPERTIES QT_RESOURCE_ALIAS ReplayGainPanelSection.qml)
set_source_files_properties(qml/ReplayGainSettingsSection.qml
    PROPERTIES QT_RESOURCE_ALIAS ReplayGainSettingsSection.qml)
# 曲目右鍵選單的「計算 ReplayGain」（T-88，單例 R128）
set_source_files_properties(qml/R128TrackMenuSection.qml
    PROPERTIES QT_RESOURCE_ALIAS R128TrackMenuSection.qml)
qt_add_qml_module(auralis_mod_hires_output_qml
    URI Auralis.Out
    VERSION 1.0
    QML_FILES
        qml/DsdPanelSection.qml
        qml/DsdSettingsSection.qml
        qml/ReplayGainPanelSection.qml
        qml/ReplayGainSettingsSection.qml
        qml/R128TrackMenuSection.qml
    SOURCES
        dsd_mode.cpp
        dsd_mode.h
        dsd_mode_controller.cpp
        dsd_mode_controller.h
        replay_gain.cpp
        replay_gain.h
        replay_gain_controller.cpp
        replay_gain_controller.h
        r128.cpp
        r128.h
        r128_controller.cpp
        r128_controller.h
)
target_include_directories(auralis_mod_hires_output_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# R128 計算（T-38，單例 R128）用 libebur128 量響度
target_link_libraries(auralis_mod_hires_output_qml PRIVATE auralis::ebur128)
# qmlcachegen 產生的 C++ 會讓 Qt 標頭出現「不可能執行到的程式碼」警告（C4702），不是我們的程式
target_compile_options(auralis_mod_hires_output_qml PRIVATE /wd4702)
target_link_libraries(auralis_mod_hires_output_qml PUBLIC auralis::core_api Qt6::Qml)
target_link_libraries(auralis_mod_hires_output PRIVATE auralis_mod_hires_output_qml
    auralis_mod_hires_output_qmlplugin)

# ASIO 輸出不在模組裡（T-103）：它是 output 類型的外掛（src/plugins/asio/，另一個 DLL），
# 模組啟動時向核心要已載入的 output 外掛，經 output_plugin_api.h 的介面註冊輸出路徑。
# 模組與主程式永遠不連結 ASIO SDK（隔離測試 boundary.no_asio_sdk_links 檢查）
target_sources(auralis_mod_hires_output PRIVATE output_plugin_api.h)

# T-30 硬體探針：獨立的命令列工具，不進主程式與打包；只在 AURALIS_WITH_ASIO=ON 時建置
if(TARGET auralis::asiosdk)
    add_executable(auralis_asio_probe
        probe/asio_probe.cpp
        probe/asio_session.cpp
        probe/asio_session.h
        probe/probe_signal.cpp
        probe/probe_signal.h
    )
    target_link_libraries(auralis_asio_probe PRIVATE auralis::asiosdk)
    # IASIO 介面的宣告需要完整的 COM 標頭（interface 巨集）
    target_compile_options(auralis_asio_probe PRIVATE /UWIN32_LEAN_AND_MEAN)
endif()
