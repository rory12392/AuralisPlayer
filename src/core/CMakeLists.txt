# 公開介面：功能模組唯一能連結的目標，include 路徑只有 src/core/api/
add_library(auralis_core_api INTERFACE)
add_library(auralis::core_api ALIAS auralis_core_api)
target_include_directories(auralis_core_api INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/api)
target_link_libraries(auralis_core_api INTERFACE Qt6::Core)

# 核心實作：內部標頭（src/core/）只對核心、主程式與核心測試開放
add_library(auralis_core STATIC
    api/auralis/audio_format.h
    api/auralis/audio_output.h
    api/auralis/audio_processor.h
    api/auralis/audio_source.h
    api/auralis/content_path.h
    api/auralis/cover_service.h
    api/auralis/dialogs.h
    api/auralis/dsd.h
    api/auralis/job_runner.h
    api/auralis/library_query.h
    api/auralis/module.h
    api/auralis/module_database.h
    api/auralis/module_settings.h
    api/auralis/navigator.h
    api/auralis/notifier.h
    api/auralis/platform_window.h
    api/auralis/playback_service.h
    api/auralis/plugin_entry.h
    api/auralis/plugins.h
    api/auralis/result.h
    api/auralis/services.h
    api/auralis/text_decoder.h
    api/auralis/track_drag_payload.h
    api/auralis/ui_registry.h
    audio/capture_backend.cpp
    audio/capture_backend.h
    audio/ffmpeg_source.cpp
    audio/ffmpeg_source.h
    audio/ffmpeg_source_decode.cpp
    audio/ffmpeg_source_impl.h
    audio/next_track.h
    audio/output_registry_impl.cpp
    audio/output_registry_impl.h
    audio/playback_engine.cpp
    audio/playback_engine.h
    audio/playback_engine_tracks.cpp
    audio/playback_engine_volume.cpp
    audio/processor_chain_impl.cpp
    audio/processor_chain_impl.h
    audio/realtime_thread.cpp
    audio/realtime_thread.h
    audio/ring_buffer.h
    audio/sample_convert.cpp
    audio/sample_convert.h
    audio/source_registry_impl.cpp
    audio/source_registry_impl.h
    audio/volume_processor.cpp
    audio/volume_processor.h
    audio/wasapi_backend.cpp
    audio/wasapi_backend.h
    audio/wasapi_endpoints.cpp
    audio/wasapi_endpoints.h
    audio/wasapi_format.cpp
    audio/wasapi_format.h
    covers/cover_image.cpp
    covers/cover_image.h
    covers/cover_image_provider.cpp
    covers/cover_image_provider.h
    covers/cover_key.cpp
    covers/cover_key.h
    covers/cover_service_impl.cpp
    covers/cover_service_impl.h
    jobs/job_runner.cpp
    library/album_regroup.cpp
    library/album_regroup.h
    library/cue_scan.cpp
    library/cue_scan.h
    library/cue_sheet.cpp
    library/cue_sheet.h
    library/cue_tracks.cpp
    library/cue_tracks.h
    library/file_enumerator.cpp
    library/file_enumerator.h
    library/library_manager.cpp
    library/library_manager.h
    library/library_query_impl.cpp
    library/library_query_impl.h
    library/library_scanner.cpp
    library/library_scanner.h
    library/library_watcher.cpp
    library/library_watcher.h
    library/library_writer.cpp
    library/library_writer.h
    library/migrations/migrations.qrc
    library/migrator.cpp
    library/migrator.h
    library/module_database_impl.cpp
    library/module_database_impl.h
    library/open_paths.cpp
    library/open_paths.h
    library/sqlite_connection.cpp
    library/read_only_stream.cpp
    library/read_only_stream.h
    library/sqlite_connection.h
    library/tag_reader.cpp
    library/tag_reader.h
    library/text_decoder.cpp
    logging/logger.cpp
    logging/logger.h
    modules/core_services.h
    modules/module_manager.cpp
    modules/module_manager.h
    modules/module_registry.cpp
    modules/module_registry.h
    platform/content_path.cpp
    platform/content_path_check.h
    platform/folder_watcher.cpp
    platform/folder_watcher.h
    platform/hresult_error.cpp
    playback/playback_queue.cpp
    plugins/plugin_loader.cpp
    plugins/plugin_loader.h
    plugins/plugin_manager.cpp
    plugins/plugin_manager.h
    plugins/plugin_manifest.cpp
    plugins/plugin_manifest.h
    plugins/plugin_registry_impl.h
    playback/playback_queue.h
    playback/playback_service_impl.cpp
    playback/playback_service_impl.h
    playback/playback_service_output.cpp
    playback/playback_service_queue.cpp
    playback/playback_service_tracks.cpp
    playback/playback_service_transport.cpp
    settings/data_dir.cpp
    settings/data_dir.h
    settings/module_settings_impl.cpp
    settings/module_settings_impl.h
    settings/output_settings.cpp
    settings/output_settings.h
    settings/volume_settings.cpp
    settings/volume_settings.h
    ui/dialogs_impl.cpp
    ui/dialogs_impl.h
    ui/ui_registry_impl.cpp
    ui/ui_registry_impl.h
)
# 主視窗的 QML 模組（src/ui）以 QML_FOREIGN 註冊核心的介面服務，需要這裡的型別資訊
qt_extract_metatypes(auralis_core)
# 升級腳本以 Qt 資源編進程式（migrations.qrc）
set_target_properties(auralis_core PROPERTIES AUTORCC ON)
# 封面服務：QImage 解碼與縮圖（Qt Gui，JPEG 經 qjpeg 外掛、PNG 內建）、image://cover 影像提供者（Qt Quick）
target_link_libraries(auralis_core PRIVATE Qt6::Gui Qt6::Quick)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_include_directories(auralis_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(auralis_core PUBLIC auralis::core_api Qt6::Core
    PRIVATE unofficial::sqlite3::sqlite3)
# TagLib（LGPL 2.1／MPL 1.1 二選一，DLL 隨程式發佈）：只有核心的掃描讀標籤
find_package(TagLib CONFIG REQUIRED)
target_link_libraries(auralis_core PRIVATE TagLib::tag)
# uchardet（MPL 1.1／GPL 2／LGPL 2.1 三選一，DLL 隨程式發佈）：TextDecoder 偵測 .cue 等文字檔的編碼
find_package(uchardet CONFIG REQUIRED)
target_link_libraries(auralis_core PRIVATE uchardet::libuchardet)
# 即時音訊執行緒登記 MMCSS「Pro Audio」
target_link_libraries(auralis_core PRIVATE avrt)
# FFmpeg（LGPL 建置，DLL 隨程式發佈）：只有核心的音源直接用
find_package(FFMPEG REQUIRED)
target_include_directories(auralis_core PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(auralis_core PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(auralis_core PRIVATE ${FFMPEG_LIBRARIES})
if(AURALIS_DEV_BUILD)
    target_compile_definitions(auralis_core PRIVATE AURALIS_DEV_BUILD)
endif()
