# 建置時（cmake -P）產生 officialPlugins()：同一次建置的官方外掛 id 與 DLL 的 SHA-256（T-103）。
# 主程式載入與匯入外掛前比對，不符就拒絕（「不是這個版本的官方外掛」）。
#   -DENTRIES=<id>|<DLL 路徑>,<id>|<DLL 路徑>…（逗號分隔，沒有外掛時是空字串）
#   -DOUT=<產生的 .cpp>
# 內容沒變時不改寫檔案，主程式才不會每次重新連結。
set(_rows "")
if(NOT ENTRIES STREQUAL "")
    string(REPLACE "," ";" _entries "${ENTRIES}")
    foreach(_entry IN LISTS _entries)
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _id)
        list(GET _parts 1 _dll)
        if(NOT EXISTS "${_dll}")
            message(FATAL_ERROR "找不到官方外掛的 DLL：${_dll}")
        endif()
        file(SHA256 "${_dll}" _hash)
        string(APPEND _rows "        {QStringLiteral(\"${_id}\"), QByteArrayLiteral(\"${_hash}\")},\n")
    endforeach()
endif()

set(_content "// 建置時由 cmake/AuralisOfficialPlugins.cmake 產生，不要手改。
#include \"plugins/plugin_manager.h\"

namespace auralis::core {

std::vector<OfficialPlugin> officialPlugins() {
    return {
${_rows}    };
}

} // namespace auralis::core
")

set(_old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" _old)
endif()
if(NOT _old STREQUAL _content)
    file(WRITE "${OUT}" "${_content}")
endif()
