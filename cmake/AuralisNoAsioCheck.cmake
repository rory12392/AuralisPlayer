# 主程式（exe）不含 ASIO 的檢查（T-103；T-105 起 ASIO 外掛內建在主程式包，ASIO 只在外掛 DLL）。兩種用法：
#
# 1. 打包後（AuralisPackaging.cmake 的 install(CODE) include 這個檔）：設 AURALIS_PACKAGE_ROOT
#    - 不能有不散布的 Microsoft 元件
#    - AuralisPlayer.exe 裡不能有 ASIO 後端或 SDK 的字串（exe 不連 SDK）
#    - AURALIS_BUNDLED_ASIO 為 ON（建置了 ASIO 外掛）時，plugins/AuralisAsio/ 要有外掛、標誌與標誌說明，
#      licenses/ 要有 ASIO SDK 的授權（T-105）；少了任何一個就失敗
#    測試：tests/boundary/check_bundled_asio.py 用假的包資料夾跑這一段
# 2. 單檔掃描（隔離測試 boundary.no_asio_sdk_strings）：cmake -DSCAN_FILE=<檔> -DEXPECT=absent|present -P 這個檔
#    present 用在 ASIO 外掛的 DLL，確認這份字串清單真的抓得到 ASIO 的程式（檢查本身有效）
#
# 字串：ASIO 後端與 SDK 的呼叫名稱（ASCII）和列舉驅動的登錄機碼（UTF-16）。ASIO SDK 的 host 檔案
# 只有用到的才會被連結進來，所以看的是任何 ASIO 程式都會有的字串，不只 SDK 的函式名稱。
set(_auralis_asio_ascii "IASIO::|ASIOInit|asiodrivers|kAsioSetIoFormat")
set(_auralis_asio_wide "SOFTWARE\\\\ASIO")

function(_auralis_asio_hits file out)
    file(STRINGS "${file}" _ascii REGEX "${_auralis_asio_ascii}")
    file(STRINGS "${file}" _wide ENCODING UTF-16LE REGEX "${_auralis_asio_wide}")
    set(${out} ${_ascii} ${_wide} PARENT_SCOPE)
endfunction()

if(DEFINED SCAN_FILE)
    if(NOT EXISTS "${SCAN_FILE}")
        message(FATAL_ERROR "找不到要掃描的檔案：${SCAN_FILE}")
    endif()
    _auralis_asio_hits("${SCAN_FILE}" _hits)
    list(LENGTH _hits _count)
    if(EXPECT STREQUAL "present" AND _count EQUAL 0)
        message(FATAL_ERROR "${SCAN_FILE} 裡找不到 ASIO 的字串：檢查清單抓不到 ASIO 的程式")
    elseif(EXPECT STREQUAL "absent" AND _count GREATER 0)
        message(FATAL_ERROR "${SCAN_FILE} 裡有 ASIO 的程式（${_count} 個字串）：${_hits}")
    endif()
    message(STATUS "ASIO 字串檢查通過：${SCAN_FILE}（${EXPECT}，${_count} 個）")
    return()
endif()

if(NOT DEFINED AURALIS_PACKAGE_ROOT)
    message(FATAL_ERROR "AuralisNoAsioCheck.cmake：要設 AURALIS_PACKAGE_ROOT 或 SCAN_FILE")
endif()
set(_forbidden vc_redist.x64.exe d3dcompiler_47.dll dxcompiler.dll dxil.dll)
foreach(_file IN LISTS _forbidden)
    if(EXISTS "${AURALIS_PACKAGE_ROOT}/${_file}")
        message(FATAL_ERROR "主程式包不應該有 ${_file}（T-103）")
    endif()
endforeach()
_auralis_asio_hits("${AURALIS_PACKAGE_ROOT}/AuralisPlayer.exe" _hits)
if(_hits)
    message(FATAL_ERROR "AuralisPlayer.exe 裡有 ASIO 的程式（T-103）：${_hits}")
endif()
if(AURALIS_BUNDLED_ASIO)
    foreach(_file plugins/AuralisAsio/AuralisAsio.dll plugins/AuralisAsio/plugin.json
            plugins/AuralisAsio/logo-dark.svg plugins/AuralisAsio/logo-light.svg
            plugins/AuralisAsio/ASIO-LOGO-README.txt licenses/ASIO-SDK.txt licenses/ASIO-SDK-host.txt)
        if(NOT EXISTS "${AURALIS_PACKAGE_ROOT}/${_file}")
            message(FATAL_ERROR "主程式包少了內建 ASIO 外掛的 ${_file}（T-105）")
        endif()
    endforeach()
    message(STATUS "主程式包內建 ASIO 外掛（含標誌與授權），exe 不含 ASIO（T-105）")
else()
    message(STATUS "主程式包沒有建置 ASIO 外掛，exe 不含 ASIO（T-103）")
endif()
