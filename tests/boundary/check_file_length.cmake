# 單檔行數上限：src/ 與 tests/ 底下的 .cpp、.h、.qml 不得超過 400 行。
# 用法：cmake -DROOT=<repo> -P check_file_length.cmake
#       cmake -DSELF_TEST=ON -DWORK_DIR=<暫存資料夾> -P check_file_length.cmake（確認檢查本身抓得到超過的檔案）

cmake_minimum_required(VERSION 3.28)

set(MAX_LINES 400)

# 回傳 root 底下超過上限的檔案（「路徑: 行數」）
function(find_long_files root dirs out_var)
    set(violations "")
    foreach(dir IN LISTS dirs)
        file(GLOB_RECURSE files LIST_DIRECTORIES false
            "${root}/${dir}/*.cpp" "${root}/${dir}/*.h" "${root}/${dir}/*.qml")
        foreach(path IN LISTS files)
            file(READ "${path}" content)
            # file(STRINGS) 會略過空行，所以數換行字元；最後一行沒有換行時也算一行
            string(REGEX MATCHALL "\n" newlines "${content}")
            list(LENGTH newlines count)
            string(LENGTH "${content}" length)
            if(length GREATER 0 AND NOT content MATCHES "\n$")
                math(EXPR count "${count} + 1")
            endif()
            if(count GREATER MAX_LINES)
                file(RELATIVE_PATH rel "${root}" "${path}")
                list(APPEND violations "${rel}: ${count} 行")
            endif()
        endforeach()
    endforeach()
    set(${out_var} "${violations}" PARENT_SCOPE)
endfunction()

if(SELF_TEST)
    set(fixture "${WORK_DIR}/file_length_selftest")
    file(REMOVE_RECURSE "${fixture}")
    string(REPEAT "x\n" 400 ok_content)
    string(REPEAT "x\n" 401 long_content)
    file(WRITE "${fixture}/src/ok.cpp" "${ok_content}")
    file(WRITE "${fixture}/src/long.h" "${long_content}")
    find_long_files("${fixture}" "src" violations)
    file(REMOVE_RECURSE "${fixture}")
    if(NOT violations STREQUAL "src/long.h: 401 行")
        message(FATAL_ERROR "自我測試失敗：預期只抓到 src/long.h（401 行），實際：${violations}")
    endif()
    message(STATUS "自我測試通過：400 行放行、401 行擋下")
    return()
endif()

if(NOT ROOT)
    message(FATAL_ERROR "請用 -DROOT=<repo 根目錄> 指定")
endif()

find_long_files("${ROOT}" "src;tests" violations)
if(violations)
    list(JOIN violations "\n  " text)
    message(FATAL_ERROR "超過 ${MAX_LINES} 行的檔案（請拆分）：\n  ${text}")
endif()
message(STATUS "單檔行數檢查通過（上限 ${MAX_LINES} 行）")
