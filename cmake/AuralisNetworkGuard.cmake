# 對外連線的建置守門：只有歌詞模組（LYR）可以連結 Qt Network（安全底線：對外連線只有 LRCLIB）。
#
# 在根 CMakeLists.txt 建好所有目標之後呼叫 auralis_check_network_links(<根目錄>)：
# 走過根目錄底下所有子目錄的目標，LINK_LIBRARIES 或 INTERFACE_LINK_LIBRARIES 直接寫到 Qt Network
# （Qt6::Network、Qt::Network、Qt6::NetworkPrivate、Qt6Network.lib…）而目標不在允許的資料夾時，設定（configure）直接失敗。
#
# 注意 Qt6::Qml 的介面本身就帶 Qt6::Network，所以連結 Qml 的目標不寫 Network 也拿得到它的標頭；
# 這一層只擋「明寫連結」，程式碼用到 QtNetwork 由 tests/boundary/check_network.py 的 include 掃描擋。

# 相對於根目錄；LYR 的程式與它自己的測試
set(AURALIS_NETWORK_ALLOWED_DIRS src/modules/lyrics tests/modules/lyrics)

function(_auralis_collect_targets dir out_var)
    get_property(targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(subdir IN LISTS subdirs)
        _auralis_collect_targets("${subdir}" sub_targets)
        list(APPEND targets ${sub_targets})
    endforeach()
    set(${out_var} ${targets} PARENT_SCOPE)
endfunction()

function(auralis_check_network_links root_dir)
    _auralis_collect_targets("${root_dir}" targets)
    set(violations "")
    foreach(target IN LISTS targets)
        set(links "")
        foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
            get_target_property(values ${target} ${property})
            if(values)
                list(APPEND links ${values})
            endif()
        endforeach()
        set(uses_network FALSE)
        foreach(item IN LISTS links)
            if(item MATCHES "Qt6?::Network|Qt6Network")
                set(uses_network TRUE)
            endif()
        endforeach()
        if(NOT uses_network)
            continue()
        endif()
        get_target_property(source_dir ${target} SOURCE_DIR)
        file(RELATIVE_PATH rel_dir "${root_dir}" "${source_dir}")
        set(allowed FALSE)
        foreach(allowed_dir IN LISTS AURALIS_NETWORK_ALLOWED_DIRS)
            if(rel_dir STREQUAL allowed_dir OR rel_dir MATCHES "^${allowed_dir}/")
                set(allowed TRUE)
            endif()
        endforeach()
        if(NOT allowed)
            list(APPEND violations "${target}（${rel_dir}）")
        endif()
    endforeach()
    if(violations)
        list(JOIN violations "、" text)
        message(FATAL_ERROR "只有歌詞模組（LYR）可以連結 Qt Network（對外連線只有 LRCLIB），這些目標違反：${text}")
    endif()
endfunction()
