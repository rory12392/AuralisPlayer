# CPACK_PROJECT_CONFIG_FILE（T-101）：cpack 每跑一個產生器讀一次，CPACK_GENERATOR 是當下那一個。
# 安裝檔的檔名和 zip 並列：AuralisPlayer-<版本>-win64-setup.exe
if(CPACK_GENERATOR STREQUAL "NSIS")
    set(CPACK_PACKAGE_FILE_NAME "AuralisPlayer-${CPACK_PACKAGE_VERSION}-win64-setup")
endif()
