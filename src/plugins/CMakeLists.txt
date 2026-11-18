# 官方外掛（T-103）：和主程式同一次建置，各自是一個 DLL，不連進主程式。
# 第一版只有 ASIO 輸出外掛（AURALIS_WITH_ASIO=ON 才建置）。
if(TARGET auralis::asiosdk)
    add_subdirectory(asio)
endif()

# 主程式內建的官方外掛雜湊：先建外掛、算 SHA-256、產生 officialPlugins()（核心的外掛管理比對）。
# 沒有建置任何外掛時清單是空的，任何外掛都會被拒絕（檔案不符）
get_property(auralis_official_entries GLOBAL PROPERTY AURALIS_OFFICIAL_PLUGINS)
get_property(auralis_official_targets GLOBAL PROPERTY AURALIS_OFFICIAL_PLUGIN_TARGETS)
set(auralis_official_dlls "")
foreach(plugin_target IN LISTS auralis_official_targets)
    list(APPEND auralis_official_dlls "$<TARGET_FILE:${plugin_target}>")
endforeach()
# 清單用逗號傳給腳本（分號會被當成指令參數的分隔）
string(REPLACE ";" "," auralis_official_arg "${auralis_official_entries}")
set(auralis_official_cpp "${CMAKE_CURRENT_BINARY_DIR}/official_plugins.cpp")
add_custom_command(OUTPUT "${auralis_official_cpp}"
    COMMAND ${CMAKE_COMMAND} "-DENTRIES=${auralis_official_arg}" "-DOUT=${auralis_official_cpp}"
            -P "${PROJECT_SOURCE_DIR}/cmake/AuralisOfficialPlugins.cmake"
    DEPENDS ${auralis_official_targets} ${auralis_official_dlls}
            "${PROJECT_SOURCE_DIR}/cmake/AuralisOfficialPlugins.cmake"
    COMMENT "產生官方外掛的雜湊清單"
    VERBATIM)
add_library(auralis_official_plugins STATIC "${auralis_official_cpp}")
target_link_libraries(auralis_official_plugins PRIVATE auralis_core)
