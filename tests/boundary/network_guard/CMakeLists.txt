# 對外連線建置守門的自我測試（tests/CMakeLists.txt 的 boundary.network_guard_*）：
# 用假的 Qt6::Network 目標與假資料夾設定一個小專案，確認歌詞模組連結時通過、
# 核心或其他模組連結時設定失敗。不需要編譯器（LANGUAGES NONE），只跑設定。
cmake_minimum_required(VERSION 3.28)
project(auralis_network_guard_fixture LANGUAGES NONE)

include(${CMAKE_CURRENT_LIST_DIR}/../../../cmake/AuralisNetworkGuard.cmake)

add_library(Qt6::Network INTERFACE IMPORTED)
add_library(Qt::Network INTERFACE IMPORTED)

# 一律有：歌詞模組連結 Network（允許）
add_subdirectory(src/modules/lyrics)
# FIXTURE_CASE=core／smtc：另外加一個違規的假目標
if(FIXTURE_CASE STREQUAL "core")
    add_subdirectory(src/core)
elseif(FIXTURE_CASE STREQUAL "smtc")
    add_subdirectory(src/modules/smtc)
endif()

auralis_check_network_links(${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS "網路守門通過")
