# 播放清單（PL）：播放清單儲存、側欄區、接受拖放與右鍵「加入播放清單 ▸」（T-50）、播放清單頁（T-55）、
# 匯入 m3u／m3u8／pls（T-51）。
auralis_add_module(auralis_mod_playlists
    SOURCES
        playlists_module.cpp
)
target_include_directories(auralis_mod_playlists PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 側欄區塊、頁面與右鍵區塊用的 QML 模組 Auralis.Pl（單例 Playlists）。靜態的 QML 外掛，
# playlists_module.cpp 用 Q_IMPORT_QML_PLUGIN 帶進來；只依賴核心 API 與 Qt
qt_add_library(auralis_mod_playlists_qml STATIC)
# 原始檔放 qml/，資源放在模組根目錄：同一個目錄的 QML 才看得到模組自己的單例（照 library_browse）
set(pl_qml_files qml/PlaylistsSection.qml qml/PlaylistPage.qml qml/PlaylistTrackMenuSection.qml
    qml/PlaylistHeader.qml qml/PlaylistTrackList.qml qml/PlaylistColumns.js qml/PlaylistListActions.js
    qml/PlaylistImportDialog.qml qml/PlaylistExportDialog.qml)
foreach(qml_file IN LISTS pl_qml_files)
    get_filename_component(qml_name ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()
qt_add_qml_module(auralis_mod_playlists_qml
    URI Auralis.Pl
    VERSION 1.0
    QML_FILES ${pl_qml_files}
    SOURCES
        playlist_store.cpp
        playlist_store.h
        playlist_store_remap.cpp
        playlists_controller.cpp
        playlists_controller.h
        playlist_tracks.cpp
        playlist_tracks.h
        playlist_tracks_selection.cpp
        playlist_tracks_edit.cpp
        playlist_parse.cpp
        playlist_parse.h
        playlist_fpl.cpp
        playlist_fpl.h
        playlist_cue.cpp
        playlist_cue.h
        playlist_import.cpp
        playlist_import_jobs.cpp
        playlist_import.h
        playlist_remap.cpp
        playlist_remap.h
        playlist_export.cpp
        playlist_export.h
        playlist_exporter.cpp
        playlist_exporter.h
)
target_include_directories(auralis_mod_playlists_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# qmlcachegen 產生的 C++ 會讓 Qt 標頭出現「不可能執行到的程式碼」警告（C4702），不是我們的程式
target_compile_options(auralis_mod_playlists_qml PRIVATE /wd4702)
target_link_libraries(auralis_mod_playlists_qml PUBLIC auralis::core_api Qt6::Qml Qt6::Gui)
target_link_libraries(auralis_mod_playlists PRIVATE auralis_mod_playlists_qml
    auralis_mod_playlists_qmlplugin)
