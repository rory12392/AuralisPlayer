# 功能模組的自動探索：每個 src/modules/<名稱>/CMakeLists.txt 自動加入，核心沒有手寫的模組清單。
#
# 模組在自己的 CMakeLists.txt 呼叫 auralis_add_module(<目標名稱> SOURCES ...)：
#   - 建成 OBJECT library，避免連結器丟掉只有靜態註冊、沒被引用的物件檔
#   - 只連結 auralis::core_api，include 路徑只有 src/core/api/，跨模組或核心內部的 include 編譯失敗
# 所有模組目標記在全域屬性 AURALIS_MODULE_TARGETS，主程式與隔離測試都從這裡連結。

function(auralis_add_module target)
    cmake_parse_arguments(ARG "" "" "SOURCES;QT_COMPONENTS" ${ARGN})
    add_library(${target} OBJECT ${ARG_SOURCES})
    target_link_libraries(${target} PRIVATE auralis::core_api)
    foreach(component IN LISTS ARG_QT_COMPONENTS)
        target_link_libraries(${target} PRIVATE Qt6::${component})
    endforeach()
    set_property(GLOBAL APPEND PROPERTY AURALIS_MODULE_TARGETS ${target})
endfunction()

function(auralis_add_modules)
    file(GLOB module_lists CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/src/modules/*/CMakeLists.txt")
    foreach(list_file IN LISTS module_lists)
        get_filename_component(module_dir "${list_file}" DIRECTORY)
        add_subdirectory("${module_dir}")
    endforeach()
endfunction()

# 把所有已探索的模組物件檔連進 target（主程式與隔離測試共用）
function(auralis_link_modules target)
    get_property(module_targets GLOBAL PROPERTY AURALIS_MODULE_TARGETS)
    # 直接連結 OBJECT library 時，CMake 會把它的物件檔加進 target
    foreach(module_target IN LISTS module_targets)
        target_link_libraries(${target} PRIVATE ${module_target})
    endforeach()
endfunction()
