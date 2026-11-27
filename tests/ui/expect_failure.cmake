# 執行 EXE（參數在 ARGS，以 | 分隔），要結束碼非 0、stderr 符合 STDERR_REGEX 才算通過。
# ctest 的 PASS_REGULAR_EXPRESSION 不看結束碼，所以「要失敗」的測試用這個包一層。
# 程式一律輸出 UTF-8；不指定 ENCODING 時 CMake 照主控台字碼頁解碼（950 時中文變亂碼、regex 對不上，P-52）
string(REPLACE "|" ";" _args "${ARGS}")
execute_process(COMMAND "${EXE}" ${_args}
    RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err ENCODING UTF8)
message("結束碼：${_rc}\nstdout：${_out}\nstderr：${_err}")
if(_rc EQUAL 0)
    message(FATAL_ERROR "應該失敗卻以結束碼 0 結束")
endif()
if(NOT _err MATCHES "${STDERR_REGEX}")
    message(FATAL_ERROR "stderr 沒有符合：${STDERR_REGEX}")
endif()
