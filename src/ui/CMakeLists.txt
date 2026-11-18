# 主視窗與共用元件的 QML 模組 Auralis.Ui：Theme 單例與 ThemeController、AppShell、共用元件、字型。
# 核心的介面服務（UiRegistry、Navigator、Dialogs、Notifier）以 QML_FOREIGN 在這裡註冊成單例。
find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Qml Quick)

qt_add_library(auralis_ui STATIC)

set(ui_qml_files
    theme/Theme.qml
    components/AppShell.qml
    components/Button.qml
    components/Checkbox.qml
    components/Chip.qml
    components/Cover.qml
    components/Dialog.qml
    components/DialogHost.qml
    components/EmptyState.qml
    components/ErrorBlock.qml
    components/FocusFrame.qml
    components/Icon.qml
    components/IconButton.qml
    components/ListHeader.qml
    components/Menu.qml
    components/MenuItem.qml
    components/NoticeBar.qml
    components/PageArea.qml
    components/Panel.qml
    components/Popover.qml
    components/ProgressLine.qml
    components/RadioRow.qml
    components/Readout.qml
    components/RingGauge.qml
    components/ScrollBar.qml
    components/SearchField.qml
    components/SegmentedControl.qml
    components/Select.qml
    components/SidePanel.qml
    components/Sidebar.qml
    components/SidebarItem.qml
    components/Skeleton.qml
    components/Slider.qml
    components/StatusTag.qml
    components/Tabs.qml
    components/TextField.qml
    components/ToolTip.qml
    components/TrackColumns.js
    components/TrackDragArea.qml
    components/TrackDropArea.qml
    components/TrackMenu.qml
    components/TrackMenuGroup.qml
    components/TrackRow.qml
    components/WheelScroll.qml
    shell/FileDropArea.qml
    shell/FileDropHint.qml
    shell/NowPlayingPanel.qml
    shell/OutputButton.qml
    shell/OutputPanel.qml
    shell/Playbar.qml
    shell/QueuePanel.qml
    shell/ScanReportDialog.qml
    shell/AsioTrademark.qml
    shell/SettingsFolders.qml
    shell/SettingsPage.qml
    shell/SettingsPlugins.qml
    shell/SettingsSection.qml
    shell/WelcomePage.qml
    shell/WindowStateKeeper.qml
)
# 元件展示頁只在開發建置（介面規範 §8）：OFF 時不編進資源
if(AURALIS_DEV_UI)
    list(APPEND ui_qml_files dev/DevUiPage.qml dev/DevFoundation.qml dev/DevControls.qml
        dev/DevDisplay.qml dev/DevLists.qml dev/DevOverlays.qml)
endif()
# 圖示（介面規範 §6.2）：SVG 只有線條資料，由 IconLibrary 讀成路徑、Icon 依主題上色
file(GLOB ui_icon_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS icons/*.svg)
set_source_files_properties(theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(auralis_ui
    URI Auralis.Ui
    VERSION 1.0
    QML_FILES ${ui_qml_files}
    SOURCES
        shell/core_bindings.cpp
        shell/core_bindings.h
        shell/file_location.cpp
        shell/file_location.h
        shell/icon_library.cpp
        shell/icon_library.h
        shell/jobs_controller.cpp
        shell/jobs_controller.h
        shell/library_controller.cpp
        shell/library_controller.h
        shell/open_files_controller.cpp
        shell/open_files_controller.h
        shell/output_controller.cpp
        shell/output_controller.h
        shell/player_controller.cpp
        shell/player_controller.h
        shell/plugins_controller.cpp
        shell/plugins_controller.h
        shell/queue_model.cpp
        shell/queue_model.h
        shell/shell_state.cpp
        shell/shell_state.h
        shell/track_drag_codec.cpp
        shell/track_drag_codec.h
        theme/theme_controller.cpp
        theme/theme_controller.h
    RESOURCES
        fonts/IBMPlexMono-Light.ttf
        fonts/IBMPlexMono-Regular.ttf
        fonts/Jost-VariableFont_wght.ttf
        ${ui_icon_files}
)
target_include_directories(auralis_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    # 產生的型別註冊檔只用檔名 include 標頭
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/shell ${CMAKE_CURRENT_SOURCE_DIR}/theme)
# qmlcachegen 產生的 C++ 會讓 Qt 標頭出現「不可能執行到的程式碼」警告（C4702），不是我們的程式
target_compile_options(auralis_ui PRIVATE /wd4702)
target_link_libraries(auralis_ui PUBLIC auralis_core Qt6::Gui Qt6::Qml Qt6::Quick)
