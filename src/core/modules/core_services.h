#pragma once

#include "audio/output_registry_impl.h"
#include "audio/processor_chain_impl.h"
#include "audio/source_registry_impl.h"
#include "covers/cover_service_impl.h"
#include "library/library_manager.h"
#include "library/library_query_impl.h"
#include "library/library_watcher.h"
#include "playback/playback_service_impl.h"
#include "plugins/plugin_registry_impl.h"
#include "ui/dialogs_impl.h"
#include "ui/ui_registry_impl.h"

#include <auralis/job_runner.h>
#include <auralis/services.h>

namespace auralis::core {

/// 核心擁有、所有模組共用的服務實例。生命週期涵蓋全部模組的 start() 到 stop()。
struct CoreServices {
    /// 已升級完成的 library.db；空字串表示沒有資料庫（模組的 ModuleDatabase 會回傳錯誤）。
    QString databasePath;
    JobRunner jobs;
    LibraryQueryImpl library{databasePath};
    /// 音樂資料夾與掃描（介面與資料夾監看用；模組只經 library 唯讀查詢）。
    LibraryManager libraryManager{jobs, library, databasePath};
    /// 音樂資料夾的監看與啟動時的增量檢查；主程式呼叫 start() 才開始（自我檢查與測試不監看）。
    LibraryWatcher libraryWatcher{libraryManager};
    OutputRegistryImpl outputs;
    SourceRegistryImpl sources;
    ProcessorChainImpl processors;
    /// 用到上面三個，所以宣告在它們之後（先建構、先解構）。
    PlaybackServiceImpl playback{sources, outputs, processors};
    /// 封面與縮圖快取（資料庫旁的 covers 資料夾）；主程式另外把 image://cover 註冊進 QML 引擎。
    CoverServiceImpl covers{databasePath};
    UiRegistryImpl ui;
    NavigatorImpl navigator{ui};
    DialogsImpl dialogs;
    NotifierImpl notifier;
    PlatformWindowImpl platformWindow;
    /// 已載入的外掛：主程式在模組啟動前由外掛管理填好（測試沒有填時是空的，等於沒有外掛）。
    PluginRegistryImpl plugins;
};

} // namespace auralis::core
