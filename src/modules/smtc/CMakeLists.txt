# 系統媒體控制（SMTC）：主視窗註冊 SMTC、鍵盤媒體鍵控制播放、播放狀態、結束時清除（T-80）；
# 曲名、藝人、專輯與封面（T-81）。
auralis_add_module(auralis_mod_smtc
    SOURCES
        smtc_mapping.cpp
        smtc_mapping.h
        smtc_module.cpp
        smtc_refresh.cpp
        smtc_refresh.h
        smtc_session.cpp
        smtc_session.h
        smtc_track.cpp
        smtc_track.h
    # 程式圖示（沒有封面時的縮圖）用 QPainter 畫成 PNG（T-81）
    QT_COMPONENTS Gui
)
target_include_directories(auralis_mod_smtc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# C++/WinRT 用 Windows SDK 內建的 cppwinrt 標頭（不經 vcpkg）；WindowsApp 提供 WinRT 的啟動函式。
# 只加在本模組目標上
target_link_libraries(auralis_mod_smtc PRIVATE WindowsApp)
# 縮圖的記憶體串流：SHCreateMemStream（shlwapi）、CreateRandomAccessStreamOverStream（shcore）（T-81）
target_link_libraries(auralis_mod_smtc PRIVATE shlwapi shcore)
