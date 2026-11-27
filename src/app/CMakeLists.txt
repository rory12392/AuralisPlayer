qt_add_executable(AuralisPlayer WIN32
    gui.cpp
    gui.h
    gui_hooks.cpp
    gui_hooks.h
    gui_screenshot.cpp
    gui_screenshot.h
    main.cpp
    play_entry.cpp
    play_entry.h
    self_check.cpp
    self_check.h
    single_instance.cpp
    single_instance.h
    soak.cpp
    soak.h
    AuralisPlayer.manifest
    AuralisPlayer.rc
)

qt_add_qml_module(AuralisPlayer
    URI Auralis.App
    VERSION 1.0
    QML_FILES Main.qml
)

target_compile_definitions(AuralisPlayer PRIVATE AURALIS_VERSION="${PROJECT_VERSION}")
if(AURALIS_DEV_UI)
    target_compile_definitions(AuralisPlayer PRIVATE AURALIS_DEV_UI)
endif()
target_link_libraries(AuralisPlayer PRIVATE auralis_core auralis_ui auralis_uiplugin Qt6::Gui Qt6::Qml
    Qt6::Quick Qt6::QuickControls2)
auralis_link_modules(AuralisPlayer)
# 外掛管理比對用的官方外掛雜湊（建置時產生；T-103）
target_link_libraries(AuralisPlayer PRIVATE auralis_official_plugins)
# --self-check 印出 FFmpeg 的版本與建置設定（T-98）；FFMPEG_* 由 src/core 的 find_package(FFMPEG) 設定
target_include_directories(AuralisPlayer PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(AuralisPlayer PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(AuralisPlayer PRIVATE ${FFMPEG_LIBRARIES})
