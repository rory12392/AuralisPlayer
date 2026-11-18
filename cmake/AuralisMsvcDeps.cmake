# 非英文版 MSVC＋Ninja 的相依標頭追蹤（/showIncludes 的前綴是本地化文字，字碼頁不同時 Ninja 認不出）
#
# cl 的 /showIncludes 前綴是本地化文字，編碼跟著「建置當下」的主控台字碼頁；CMake 卻依
# 「產生建置檔當下」的字碼頁寫進 rules.ninja。兩者不同時 Ninja 記不到相依標頭，改了標頭不會重編。
# 做法：啟動器把 cl 的前綴行改寫成固定的 ASCII 標記，Ninja 也只認這個標記，兩邊都與字碼頁無關。
# 英文版 MSVC 的前綴本來就是 ASCII，不需要這些設定。
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND CMAKE_CXX_CL_SHOWINCLUDES_PREFIX)
    string(REGEX REPLACE "[ -~]" "" _auralis_non_ascii "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}")
    if(NOT _auralis_non_ascii STREQUAL "")
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        # 前綴以 UTF-8 十六進位傳給啟動器，建置檔裡不出現非 ASCII 字元
        string(HEX "${CMAKE_CXX_CL_SHOWINCLUDES_PREFIX}" _auralis_prefix_hex)
        set(CMAKE_CXX_COMPILER_LAUNCHER
            "${Python3_EXECUTABLE};-I;-S;${CMAKE_CURRENT_LIST_DIR}/msvc_showincludes.py;${_auralis_prefix_hex};AURALIS_SHOWINCLUDES:")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "AURALIS_SHOWINCLUDES:")
        # C++20 模組掃描的規則不經過啟動器，前綴同樣對不上；本專案沒有用 C++ 模組，關掉
        set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
        message(STATUS "MSVC /showIncludes 前綴非 ASCII：改用啟動器追蹤相依標頭")
    endif()
endif()
