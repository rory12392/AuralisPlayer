cmake_minimum_required(VERSION 3.28)

# vcpkg 的 DLL（FFmpeg、SQLite…）跟著 install(TARGETS) 一起安裝進 zip；
# vcpkg toolchain 在 project() 載入時就看這個值，所以要在 project() 之前設
set(X_VCPKG_APPLOCAL_DEPS_INSTALL ON CACHE BOOL "安裝時一併複製 vcpkg 的 DLL")

# AURALIS_WITH_ASIO：建置 ASIO 外掛（src/plugins/asio/，另一個 DLL）。主程式與 OUT 模組永遠不連結 SDK。
# ASIO SDK 是 Steinberg 專有／GPLv3 雙授權，本專案採 GPLv3（整個專案 GPLv3，T-105）；ASIO 外掛隨主程式包
# 預先安裝（plugins\AuralisAsio\），也另外打一個外掛包。
# 開發建置預設不下載；發佈用的 windows-release preset 是 ON（建置外掛，STACK §12）。
# vcpkg 在 project() 載入時就決定要裝哪些特性，選項與特性都要在 project() 之前
option(AURALIS_WITH_ASIO "建置 ASIO 外掛：開啟時 vcpkg 會下載 Steinberg ASIO SDK" OFF)
if(AURALIS_WITH_ASIO)
    list(APPEND VCPKG_MANIFEST_FEATURES "asio")
endif()

project(AuralisPlayer VERSION 1.0.0 DESCRIPTION "Windows 桌面音樂播放器" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(AURALIS_DEV_BUILD "開發版：沒有設定 AURALIS_DATA_DIR 時拒絕啟動，保護使用者的真實資料" ON)
option(AURALIS_BUILD_TESTS "建置測試" ON)
option(AURALIS_DEV_UI "元件展示頁 dev/ui（只在開發建置；發佈建置要 OFF）" OFF)

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
# 中文版 MSVC 的相依標頭追蹤要在建立任何目標之前設定
include(cmake/AuralisMsvcDeps.cmake)
include(cmake/AuralisHiresDeps.cmake)

# 免安裝 zip 的 exe 與 Qt DLL 放在根目錄；Qt 的部署腳本在 qt_standard_project_setup 時就讀這個值
set(CMAKE_INSTALL_BINDIR "." CACHE PATH "安裝時執行檔的位置（相對於安裝根目錄）")

# Network 只給歌詞模組（LYR）連 LRCLIB；其他目標連結它時設定失敗（cmake/AuralisNetworkGuard.cmake）
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network)
qt_standard_project_setup(REQUIRES 6.11)
# QML 模組照 URI 放在 build/<preset>/qml/ 底下，qmllint 才找得到 import Auralis.Ui
set(QT_QML_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml)

add_subdirectory(src/core)
add_subdirectory(src/ui)

include(cmake/AuralisModules.cmake)
auralis_add_modules()
# 官方外掛（另外的 DLL）與主程式內建的外掛雜湊
add_subdirectory(src/plugins)

add_subdirectory(src/app)

if(AURALIS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/AuralisNetworkGuard.cmake)
auralis_check_network_links(${PROJECT_SOURCE_DIR})

include(cmake/AuralisPackaging.cmake)
