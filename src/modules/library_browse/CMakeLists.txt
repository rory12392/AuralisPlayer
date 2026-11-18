# 音樂庫瀏覽與搜尋（LIB）：搜尋與篩選的服務（T-20）、所有曲目頁與篩選列、搜尋框（T-21）、
# 專輯頁（T-22）、藝人頁與資料夾頁（T-23）、專輯詳細頁（T-24）、清單的選取、拖放與右鍵選單（T-25）。
auralis_add_module(auralis_mod_library_browse
    SOURCES
        library_browse_module.cpp
)
target_include_directories(auralis_mod_library_browse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# 給頁面用的 QML 模組 Auralis.Lib（單例 LibrarySearch）。靜態的 QML 外掛，
# 模組的 library_browse_module.cpp 用 Q_IMPORT_QML_PLUGIN 帶進來；只依賴核心 API 與 Qt
qt_add_library(auralis_mod_library_browse_qml STATIC)
# 頁面原始檔放 qml/，資源放在模組根目錄：同一個目錄的 QML 才看得到模組自己的單例（照 hires_output）
set(lib_qml_files qml/LibraryFilterBar.qml qml/LibrarySearchBox.qml qml/TracksPage.qml
    qml/AlbumsPage.qml qml/AlbumGrid.qml qml/AlbumList.qml qml/AlbumDetailList.qml
    qml/LibraryTrackList.qml qml/ArtistsPage.qml qml/FoldersPage.qml qml/FolderTreeRow.qml
    qml/AlbumPage.qml qml/LibraryTrackMenuSection.qml qml/ListActions.js
    qml/TrackColumnsButton.qml qml/TrackColumnCatalog.js)
foreach(qml_file IN LISTS lib_qml_files)
    get_filename_component(qml_name ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_name})
endforeach()
qt_add_qml_module(auralis_mod_library_browse_qml
    URI Auralis.Lib
    VERSION 1.0
    QML_FILES ${lib_qml_files}
    SOURCES
        album_detail_controller.cpp
        album_detail_controller.h
        album_list.cpp
        album_list.h
        albums_controller.cpp
        albums_controller.h
        albums_model.cpp
        albums_model.h
        artist_list.cpp
        artist_list.h
        artists_controller.cpp
        artists_controller.h
        artists_model.cpp
        artists_model.h
        folder_tree.cpp
        folder_tree.h
        folders_controller.cpp
        folders_controller.h
        folders_model.cpp
        folders_model.h
        library_playback.cpp
        library_playback.h
        search_controller.cpp
        search_controller.h
        search_criteria.cpp
        search_criteria.h
        search_results_model.cpp
        search_results_model.h
        track_columns_setting.cpp
        track_columns_setting.h
        track_selection.cpp
        track_selection.h
        track_text.cpp
        track_text.h
)
target_include_directories(auralis_mod_library_browse_qml PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# qmlcachegen 產生的 C++ 會讓 Qt 標頭出現「不可能執行到的程式碼」警告（C4702），不是我們的程式
target_compile_options(auralis_mod_library_browse_qml PRIVATE /wd4702)
# Qt6::Gui：選取的 Ctrl／Shift＋點擊讀 QGuiApplication::keyboardModifiers（T-25）
target_link_libraries(auralis_mod_library_browse_qml PUBLIC auralis::core_api Qt6::Qml Qt6::Gui)
target_link_libraries(auralis_mod_library_browse PRIVATE auralis_mod_library_browse_qml
    auralis_mod_library_browse_qmlplugin)
