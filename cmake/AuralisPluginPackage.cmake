# ASIO 外掛包（T-103；T-105 起主程式包也內建同一份，這個包給更新或重新安裝用）：
# cmake --build --preset windows-release --target package_plugin_asio
# 由 src/plugins/asio/CMakeLists.txt 的自訂目標以 cmake -P 執行。產生
#   <OUT_DIR>/<NAME>.zip
#     <NAME>/AuralisAsio/AuralisAsio.dll、plugin.json、logo-dark.svg、logo-light.svg、ASIO-LOGO-README.txt
#       （外掛資料夾的檔案和主程式包 plugins\AuralisAsio\ 的相同）
#     <NAME>/LICENSE（GPLv3 全文：整個專案以 GPLv3 散布）
#     <NAME>/THIRD-PARTY-NOTICES.txt（外掛自己的）
#     <NAME>/INSTALL.txt（中文的安裝方式；檔名用 ASCII：cmake -E tar 的 zip 檔名用系統字碼頁、
#       不標 UTF-8，中文檔名在別的語系會變亂碼）
#     <NAME>/licenses/ASIO-SDK.txt、ASIO-SDK-host.txt
# 打包後列出 zip 的內容核對，少了任何一個就失敗。
# 變數：DLL、PLUGIN_DIR（建置資料夾的外掛資料夾）、LICENSE、NOTICES、INSTALL_GUIDE、ASIO_LICENSE、
#       ASIO_HOST_LICENSE、STAGE_DIR、OUT_DIR、NAME
set(_root "${STAGE_DIR}/${NAME}")
file(REMOVE_RECURSE "${STAGE_DIR}")
file(MAKE_DIRECTORY "${_root}/AuralisAsio" "${_root}/licenses")

set(_plugin_files AuralisAsio.dll plugin.json logo-dark.svg logo-light.svg ASIO-LOGO-README.txt)
file(COPY_FILE "${DLL}" "${_root}/AuralisAsio/AuralisAsio.dll")
foreach(_file IN LISTS _plugin_files)
    if(NOT _file STREQUAL "AuralisAsio.dll")
        file(COPY_FILE "${PLUGIN_DIR}/${_file}" "${_root}/AuralisAsio/${_file}")
    endif()
endforeach()
file(COPY_FILE "${LICENSE}" "${_root}/LICENSE")
file(COPY_FILE "${NOTICES}" "${_root}/THIRD-PARTY-NOTICES.txt")
file(COPY_FILE "${INSTALL_GUIDE}" "${_root}/INSTALL.txt")
file(COPY_FILE "${ASIO_LICENSE}" "${_root}/licenses/ASIO-SDK.txt")
file(COPY_FILE "${ASIO_HOST_LICENSE}" "${_root}/licenses/ASIO-SDK-host.txt")

set(_zip "${OUT_DIR}/${NAME}.zip")
file(REMOVE "${_zip}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar cf "${_zip}" --format=zip "${NAME}"
    WORKING_DIRECTORY "${STAGE_DIR}" RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "外掛包壓縮失敗：${_result}")
endif()

# 打包後檢查：照 zip 實際的內容核對
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar tf "${_zip}" OUTPUT_VARIABLE _listing
    RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "讀不到外掛包的內容：${_zip}")
endif()
string(REPLACE "\\" "/" _listing "${_listing}")
set(_expected LICENSE THIRD-PARTY-NOTICES.txt INSTALL.txt licenses/ASIO-SDK.txt
    licenses/ASIO-SDK-host.txt)
foreach(_file IN LISTS _plugin_files)
    list(APPEND _expected "AuralisAsio/${_file}")
endforeach()
foreach(_file IN LISTS _expected)
    string(FIND "${_listing}" "${NAME}/${_file}" _found)
    if(_found EQUAL -1)
        message(FATAL_ERROR "外掛包少了 ${_file}（T-103、T-105）")
    endif()
endforeach()
message(STATUS "外掛包：${_zip}")
