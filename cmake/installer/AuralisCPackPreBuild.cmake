# CPACK_PRE_BUILD_SCRIPTS（T-101）：cpack 把檔案裝進暫存區之後、產生安裝檔之前執行。
# 安裝檔不放 portable.flag（安裝版的資料在 %LOCALAPPDATA%\AuralisPlayer，不在程式旁邊）；zip 照舊保留。
# 刪掉之後檢查安裝內容，不符就讓打包失敗（照 AuralisPackaging.cmake 的 FATAL_ERROR 寫法）
if(NOT CPACK_GENERATOR STREQUAL "NSIS")
    return()
endif()

set(_auralis_root "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
if(NOT IS_DIRECTORY "${_auralis_root}")
    message(FATAL_ERROR "找不到安裝檔的暫存區 ${_auralis_root}（T-101）")
endif()

file(REMOVE "${_auralis_root}/portable.flag")

if(EXISTS "${_auralis_root}/portable.flag")
    message(FATAL_ERROR "安裝檔的內容不應該有 portable.flag（T-101，STACK §9）")
endif()
# 內建的 ASIO 外掛、標誌與授權由 AuralisPackaging.cmake 的打包後檢查核對（T-105），這裡不重複
set(_auralis_files AuralisPlayer.exe LICENSE THIRD-PARTY-NOTICES.txt)
foreach(_auralis_file ${_auralis_files})
    if(NOT EXISTS "${_auralis_root}/${_auralis_file}")
        message(FATAL_ERROR "安裝檔的內容少了 ${_auralis_file}（T-101，STACK §9）")
    endif()
endforeach()
