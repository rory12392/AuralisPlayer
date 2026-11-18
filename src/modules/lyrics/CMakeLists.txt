# 歌詞與沉浸（LYR）：LRC 解析、來源鏈（.lrc → 內嵌）與 lyrics_cache（T-65）；
# 歌詞面板與正在播放面板的歌詞預覽（T-66）；LRCLIB 查詢與設定頁的歌詞分區（T-67）。
auralis_add_module(auralis_mod_lyrics
    SOURCES
        lyrics_module.cpp
)
# 對外連線只有 LRCLIB，網路只給本模組（核心介面「T-78 定稿」；其他目標連結會被建置守門擋下）
target_link_libraries(auralis_mod_lyrics PRIVATE Qt6::Network)
# 模組的標頭以模組資料夾為根（#include "lyrics_loader.h"）
target_include_directories(auralis_mod_lyrics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 面板與預覽的 QML 模組 Auralis.Lyr（單例 Lyrics）。單例要用載入器，所以 T-65 的解析、來源鏈、
# 快取與載入器一起放進這個靜態函式庫（照 library_browse）；模組的 lyrics_module.cpp 用
# Q_IMPORT_QML_PLUGIN 帶進外掛。只依賴核心 API 與 Qt
qt_add_library(auralis_mod_lyrics_qml STATIC)
# 原始檔放 qml/，資源放在模組根目錄：同一個目錄的 QML 才看得到模組自己的單例
set(lyr_qml_files qml/LyricsPanel.qml qml/LyricsList.qml qml/LyricsPreview.qml
    qml/LyricsSettingsSection.qml qml/FullscreenPage.qml)
foreach(qml_file IN LISTS lyr_qml_files)
    get_filename_component(qml_name ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()
qt_add_qml_module(auralis_mod_lyrics_qml
    URI Auralis.Lyr
    VERSION 1.0
    QML_FILES ${lyr_qml_files}
    SOURCES
        backdrop.cpp
        backdrop.h
        lrclib.cpp
        lrclib.h
        lyrics_cache.cpp
        lyrics_cache.h
        lyrics_document.cpp
        lyrics_document.h
        lyrics_finder.cpp
        lyrics_finder.h
        lyrics_loader.cpp
        lyrics_loader.h
        lyrics_view.cpp
        lyrics_view.h
)
target_include_directories(auralis_mod_lyrics_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# qmlcachegen 產生的 C++ 會讓 Qt 標頭出現「不可能執行到的程式碼」警告（C4702），不是我們的程式
target_compile_options(auralis_mod_lyrics_qml PRIVATE /wd4702)
target_link_libraries(auralis_mod_lyrics_qml PUBLIC auralis::core_api Qt6::Qml)
target_link_libraries(auralis_mod_lyrics PRIVATE auralis_mod_lyrics_qml
    auralis_mod_lyrics_qmlplugin)
