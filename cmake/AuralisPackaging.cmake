# 免安裝 zip：cmake --build --preset windows-release --target package
# exe、Qt DLL 與 QML 外掛放在 zip 的根目錄，旁邊的 portable.flag 讓程式把資料放在 <exe 所在>\data\。

# vcpkg 的 DLL（FFmpeg、SQLite…）由根 CMakeLists.txt 的 X_VCPKG_APPLOCAL_DEPS_INSTALL 跟著 exe 安裝
install(TARGETS AuralisPlayer
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# 不帶 Microsoft 的 D3D／DXC 著色器編譯器與 vc_redist.x64.exe（T-102 實測程式不載入前三個；
# VC++ 執行階段由使用者安裝，README 寫明，T-103）
qt_generate_deploy_qml_app_script(
    TARGET AuralisPlayer
    OUTPUT_SCRIPT auralis_deploy_script
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    DEPLOY_TOOL_OPTIONS --no-system-d3d-compiler --no-system-dxc-compiler
)
install(SCRIPT ${auralis_deploy_script})

# 歌詞模組（LYR）用 https 連 LRCLIB：zip 要有 Qt Network 與 Windows 內建的 TLS 後端（Schannel），
# 不帶 OpenSSL 後端（windeployqt 預設就不帶；帶了要另外附 OpenSSL 與它的授權）。部署後檢查，少了或多了就讓打包失敗
install(CODE [[
    set(_auralis_root "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}")
    foreach(_auralis_file Qt6Network.dll plugins/tls/qschannelbackend.dll)
        if(NOT EXISTS "${_auralis_root}/${_auralis_file}")
            message(FATAL_ERROR "打包內容少了 ${_auralis_file}（歌詞模組連 LRCLIB 需要）")
        endif()
    endforeach()
    if(EXISTS "${_auralis_root}/plugins/tls/qopensslbackend.dll")
        message(FATAL_ERROR "打包內容不應該有 OpenSSL 的 TLS 後端（plugins/tls/qopensslbackend.dll）")
    endif()
]])

# 授權檔（T-98）：根目錄放 LICENSE（本專案 GPLv3 全文，T-105）與 THIRD-PARTY-NOTICES.txt；第三方授權全文
# 放 licenses/，從 vcpkg 安裝的 share/<port>/copyright 與 repo 的字型授權照原樣複製（不手抄）。
# 安裝檔（T-101）沿用這些 install() 規則，不另外列授權檔
set(_auralis_vcpkg_share "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share")
install(FILES LICENSE THIRD-PARTY-NOTICES.txt DESTINATION ${CMAKE_INSTALL_BINDIR})
# Qt 的 LGPLv3 以 GPLv3 為基礎，要附 GPLv3 全文（就是 LICENSE）
install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses RENAME GPL-3.0.txt)
# 官方 ASIO 外掛預先安裝在程式資料夾的 plugins\AuralisAsio\（T-105，PRD v1.8）：和外掛包同一份檔案，
# 程式啟動時當成內建外掛載入（預設啟用、可以停用、不能移除）。ASIO SDK 只在外掛 DLL，exe 不連 SDK
if(TARGET auralis_plugin_asio)
    set(_auralis_asio_dir "${CMAKE_BINARY_DIR}/plugins/${AURALIS_ASIO_PLUGIN_ID}")
    install(FILES "$<TARGET_FILE:auralis_plugin_asio>" "${_auralis_asio_dir}/plugin.json"
        "${_auralis_asio_dir}/logo-dark.svg" "${_auralis_asio_dir}/logo-light.svg"
        "${_auralis_asio_dir}/ASIO-LOGO-README.txt"
        DESTINATION ${CMAKE_INSTALL_BINDIR}/plugins/${AURALIS_ASIO_PLUGIN_ID})
    install(FILES "${AURALIS_ASIO_LICENSE}" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
        RENAME ASIO-SDK.txt)
    install(FILES "${AURALIS_ASIO_HOST_LICENSE}" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses)
    set(_auralis_bundled_asio ON)
else()
    set(_auralis_bundled_asio OFF)
endif()
install(FILES "${_auralis_vcpkg_share}/ffmpeg/copyright" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME FFmpeg.txt)
install(FILES "${_auralis_vcpkg_share}/taglib/copyright" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME TagLib.txt)
install(FILES "${_auralis_vcpkg_share}/taglib/COPYING.MPL" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME TagLib-MPL-1.1.txt)
install(FILES "${_auralis_vcpkg_share}/utfcpp/copyright" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME utfcpp.txt)
install(FILES "${_auralis_vcpkg_share}/uchardet/copyright" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME uchardet.txt)
install(FILES "${_auralis_vcpkg_share}/sqlite3/copyright" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME SQLite.txt)
install(FILES "${_auralis_vcpkg_share}/libebur128/copyright" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME libebur128.txt)
install(FILES "${_auralis_vcpkg_share}/zlib/copyright" DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses
    RENAME zlib.txt)
install(FILES src/ui/fonts/OFL-Jost.txt src/ui/fonts/OFL-IBMPlexMono.txt
    DESTINATION ${CMAKE_INSTALL_BINDIR}/licenses)
# 部署後檢查授權檔：少了就讓打包失敗
set(_auralis_license_files LICENSE THIRD-PARTY-NOTICES.txt licenses/GPL-3.0.txt licenses/FFmpeg.txt
    licenses/TagLib.txt licenses/TagLib-MPL-1.1.txt licenses/utfcpp.txt licenses/uchardet.txt
    licenses/SQLite.txt licenses/libebur128.txt licenses/zlib.txt licenses/OFL-Jost.txt
    licenses/OFL-IBMPlexMono.txt)
install(CODE "
    set(_auralis_root \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    foreach(_auralis_file ${_auralis_license_files})
        if(NOT EXISTS \"\${_auralis_root}/\${_auralis_file}\")
            message(FATAL_ERROR \"打包內容少了授權檔 \${_auralis_file}（T-98，STACK §9）\")
        endif()
    endforeach()
")
# 主程式包不能有不散布的 Microsoft 元件（T-103）；exe 裡不能有 ASIO SDK 的程式（字串）；
# 建置了 ASIO 外掛時要有內建的外掛、標誌與授權（T-105）
install(CODE "
    set(AURALIS_PACKAGE_ROOT \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
    set(AURALIS_BUNDLED_ASIO ${_auralis_bundled_asio})
    include(\"${PROJECT_SOURCE_DIR}/cmake/AuralisNoAsioCheck.cmake\")
")

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/portable.flag "")
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/portable.flag DESTINATION ${CMAKE_INSTALL_BINDIR})

set(CPACK_GENERATOR ZIP)
set(CPACK_PACKAGE_NAME AuralisPlayer)
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "AuralisPlayer-${PROJECT_VERSION}-win64")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)

# 安裝檔（T-101）：cmake --build --preset windows-release --target package_installer（要先裝 NSIS 3.x）
# 和 zip 同一個建置、同一套 install() 規則；package 目標照舊只產生 zip（CI 不需要 NSIS）。
# 範本 cmake/installer/NSIS.template.in 取代 CMake 內建的（只裝給目前使用者、繁體中文、不放 portable.flag）
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/installer")
set(CPACK_PACKAGE_VENDOR rory12392)
set(CPACK_PACKAGE_INSTALL_DIRECTORY AuralisPlayer)
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY AuralisPlayer)
set(CPACK_NSIS_PACKAGE_NAME AuralisPlayer)
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/src/app/AuralisPlayer.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/src/app/AuralisPlayer.ico")
# 安裝畫面顯示本專案的授權（LICENSE，GPLv3 全文；整個專案改 GPLv3，PRD v1.8，T-105）
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/installer/AuralisCPackProject.cmake")
set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/installer/AuralisCPackPreBuild.cmake")
include(CPack)

add_custom_target(package_installer
    COMMAND "${CMAKE_CPACK_COMMAND}" -G NSIS --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake"
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    USES_TERMINAL
    COMMENT "產生 Windows 安裝檔（NSIS）")
add_dependencies(package_installer AuralisPlayer)
