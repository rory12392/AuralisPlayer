# ASIO 輸出外掛（T-103）：AuralisAsio.dll＋plugin.json，output 類型，和主程式同一次建置的官方外掛。
# 只在 AURALIS_WITH_ASIO=ON（有 auralis::asiosdk）時建置；主程式與 OUT 模組永遠不連結 ASIO SDK。
# 整個專案以 GPLv3 散布（ASIO SDK 授權檔的 GPLv3 選項）；外掛隨主程式包預先安裝（cmake/AuralisPackaging.cmake，
# T-105），也另外打一個 package_plugin_asio（重新安裝用）。

set(AURALIS_ASIO_PLUGIN_ID AuralisAsio CACHE INTERNAL "ASIO 外掛的 id（也是外掛資料夾名稱）")
set(auralis_asio_plugin_dir "${CMAKE_BINARY_DIR}/plugins/${AURALIS_ASIO_PLUGIN_ID}")

# 後端本體：外掛 DLL 與 OUT 的 ASIO 測試（假的 IASIO 驅動）共用。原本在 OUT 模組，只搬位置、不改行為
add_library(auralis_asio_impl STATIC
    asio_backend.cpp
    asio_backend.h
    asio_backend_factory.cpp
    asio_backend_factory.h
    asio_control_thread.cpp
    asio_control_thread.h
    asio_drivers.cpp
    asio_drivers.h
    asio_dsd.cpp
    asio_dsd.h
    asio_render.cpp
    asio_samples.cpp
    asio_samples.h
)
# 後端用 OUT 的 DopSink（DoP 退路）與 OUT 和外掛之間的介面標頭（output_plugin_api.h）
target_include_directories(auralis_asio_impl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src/modules/hires_output)
target_link_libraries(auralis_asio_impl PUBLIC auralis::core_api auralis::asiosdk)
# IASIO 介面的宣告需要完整的 COM 標頭（interface 巨集）
set_source_files_properties(asio_backend.cpp asio_drivers.cpp asio_dsd.cpp asio_render.cpp
    PROPERTIES COMPILE_OPTIONS /UWIN32_LEAN_AND_MEAN)

add_library(auralis_plugin_asio SHARED
    asio_plugin.cpp
    # DoP 包裝器與 HRESULT 轉錯誤訊息編進外掛自己的一份（OUT 模組與核心的物件檔在主程式裡，DLL 拿不到）
    ${PROJECT_SOURCE_DIR}/src/modules/hires_output/dop_sink.cpp
    ${PROJECT_SOURCE_DIR}/src/core/platform/hresult_error.cpp
)
target_link_libraries(auralis_plugin_asio PRIVATE auralis_asio_impl)
target_compile_definitions(auralis_plugin_asio PRIVATE
    AURALIS_ASIO_PLUGIN_ID="${AURALIS_ASIO_PLUGIN_ID}")
set_target_properties(auralis_plugin_asio PROPERTIES
    OUTPUT_NAME AuralisAsio
    RUNTIME_OUTPUT_DIRECTORY "${auralis_asio_plugin_dir}"
    # 多組態產生器也放同一個資料夾
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${auralis_asio_plugin_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${auralis_asio_plugin_dir}")

# plugin.json：和主程式同一個版本，只給這一版主程式用
configure_file(plugin.json.in "${auralis_asio_plugin_dir}/plugin.json" @ONLY)

# ASIO Compatible 標誌（Steinberg 商標規則第 1 條，T-105）：repo 的 logo/ 是 SDK 附的原檔（深色主題用
# 白色透明版、淺色用黑白版），原樣複製到外掛資料夾（plugin.json 的 logo 欄位），介面依主題讀。
# 少了就設定失敗：發佈包一定要有標誌
set(auralis_asio_logos dark light)
foreach(theme IN LISTS auralis_asio_logos)
    set(logo "${CMAKE_CURRENT_SOURCE_DIR}/logo/asio-compatible-logo-${theme}.svg")
    if(NOT EXISTS "${logo}")
        message(FATAL_ERROR "找不到 ASIO Compatible 標誌：${logo}（T-105）")
    endif()
    configure_file("${logo}" "${auralis_asio_plugin_dir}/logo-${theme}.svg" COPYONLY)
endforeach()
configure_file(logo/README.txt "${auralis_asio_plugin_dir}/ASIO-LOGO-README.txt" COPYONLY)

# SDK 的 host/ 檔案（asiodrivers、asiolist、ginclude）帶自己的 BSD 式授權：從 vcpkg 安裝的標頭照原文擷取
# （T-102），主程式包（內建外掛）與外掛包都用這一份
set(auralis_vcpkg_installed "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
set(AURALIS_ASIO_LICENSE "${auralis_vcpkg_installed}/share/asiosdk/copyright"
    CACHE INTERNAL "ASIO SDK 的授權檔")
set(AURALIS_ASIO_HOST_LICENSE "${CMAKE_BINARY_DIR}/plugin-licenses/ASIO-SDK-host.txt"
    CACHE INTERNAL "ASIO SDK host 檔案的授權（建置時擷取）")
include("${PROJECT_SOURCE_DIR}/cmake/AuralisAsioHostLicense.cmake")
auralis_asio_host_license("${auralis_vcpkg_installed}/include/asiosdk/host/asiodrivers.h"
    "${AURALIS_ASIO_HOST_LICENSE}")

# 外掛包（另一個 zip，給更新或重新安裝用；內容和主程式包內建的那一份相同，T-105）：
# cmake --build --preset windows-release --target package_plugin_asio
set(auralis_plugin_package "AuralisPlayer-${PROJECT_VERSION}-plugin-asio-win64")
add_custom_target(package_plugin_asio
    COMMAND ${CMAKE_COMMAND}
        "-DDLL=$<TARGET_FILE:auralis_plugin_asio>"
        "-DPLUGIN_DIR=${auralis_asio_plugin_dir}"
        "-DLICENSE=${PROJECT_SOURCE_DIR}/LICENSE"
        "-DNOTICES=${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt"
        "-DINSTALL_GUIDE=${CMAKE_CURRENT_SOURCE_DIR}/install.txt"
        "-DASIO_LICENSE=${AURALIS_ASIO_LICENSE}"
        "-DASIO_HOST_LICENSE=${AURALIS_ASIO_HOST_LICENSE}"
        "-DSTAGE_DIR=${CMAKE_BINARY_DIR}/plugin-package"
        "-DOUT_DIR=${CMAKE_BINARY_DIR}"
        "-DNAME=${auralis_plugin_package}"
        -P "${PROJECT_SOURCE_DIR}/cmake/AuralisPluginPackage.cmake"
    DEPENDS auralis_plugin_asio
    COMMENT "打包 ASIO 外掛"
    VERBATIM)

# 官方雜湊：建好 DLL 後算 SHA-256，主程式比對（src/plugins/CMakeLists.txt 收集）
set_property(GLOBAL APPEND PROPERTY AURALIS_OFFICIAL_PLUGINS
    "${AURALIS_ASIO_PLUGIN_ID}|$<TARGET_FILE:auralis_plugin_asio>")
set_property(GLOBAL APPEND PROPERTY AURALIS_OFFICIAL_PLUGIN_TARGETS auralis_plugin_asio)
