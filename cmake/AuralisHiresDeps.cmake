# 進階輸出的建置依賴：libebur128（R128 響度）與選用的 Steinberg ASIO SDK。
# 這裡只提供可連結的目標。
#
#   auralis::ebur128   一律有（OUT 模組）
#   auralis::asiosdk   只在 AURALIS_WITH_ASIO=ON 時有；連結它的目標會得到編譯巨集 AURALIS_WITH_ASIO=1
#
# auralis::asiosdk 只給 ASIO 外掛（src/plugins/asio/）、ASIO 硬體探針與它們的測試連結；
# 主程式、核心與模組不能連結（T-103，隔離測試 boundary.no_asio_sdk_links 檢查）。

# vcpkg 的 libebur128 沒有附 CMake 設定檔，自己包成匯入目標
find_path(AURALIS_EBUR128_INCLUDE_DIR ebur128.h REQUIRED)
find_library(AURALIS_EBUR128_LIBRARY ebur128 REQUIRED)
add_library(auralis::ebur128 UNKNOWN IMPORTED)
set_target_properties(auralis::ebur128 PROPERTIES
    IMPORTED_LOCATION "${AURALIS_EBUR128_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${AURALIS_EBUR128_INCLUDE_DIR}"
)

if(AURALIS_WITH_ASIO)
    # SDK 由 vcpkg 的 asio 特性下載到建置資料夾（根 CMakeLists.txt 在 project() 之前開啟），不進 repo
    find_package(unofficial-asiosdk CONFIG REQUIRED)
    # vcpkg 的目標把 SDK 的原始檔直接加進使用者的目標，會套用本專案的 /W4 /WX；
    # 所以另外編成一個關掉警告的靜態函式庫，使用者只看到標頭
    get_target_property(asiosdk_sources unofficial::asiosdk::host INTERFACE_SOURCES)
    get_target_property(asiosdk_includes unofficial::asiosdk::host INTERFACE_INCLUDE_DIRECTORIES)
    add_library(auralis_asiosdk STATIC ${asiosdk_sources})
    target_include_directories(auralis_asiosdk SYSTEM PUBLIC ${asiosdk_includes})
    target_link_libraries(auralis_asiosdk PUBLIC ole32 uuid)
    # 整個換掉全域的 /W4 /WX，並取消三個全域定義：SDK 需要完整的 Windows 標頭（COM 的 interface 巨集），
    # 列舉驅動的程式用 char 字串呼叫登錄 API（ANSI 版）。cl 會各印一行 D9025（覆寫 /D），不是錯誤
    set_target_properties(auralis_asiosdk PROPERTIES
        COMPILE_OPTIONS "/w;/UWIN32_LEAN_AND_MEAN;/UUNICODE;/U_UNICODE")
    target_compile_definitions(auralis_asiosdk PUBLIC AURALIS_WITH_ASIO=1)
    add_library(auralis::asiosdk ALIAS auralis_asiosdk)
endif()
message(STATUS "AURALIS_WITH_ASIO=${AURALIS_WITH_ASIO}")
