#pragma once

#include <QString>

#include <memory>

namespace auralis {

class JobRunner;
class LibraryQuery;
class ModuleDatabase;
class ModuleSettings;
class PlaybackService;
class OutputRegistry;
class SourceRegistry;
class ProcessorChain;
class CoverService;
class UiRegistry;
class Navigator;
class Dialogs;
class Notifier;
class PlatformWindow;
class PluginRegistry;

/// 模組的基本資料。code 是模組代碼（例：LIB），也是設定段落 modules/<code>/ 的名稱。
struct ModuleInfo {
    QString code;
    QString name;
    QString version;
};

/// 核心交給模組的服務入口。每個模組拿到自己的一份（database()、settings() 只開放該模組的範圍）。
/// 只在 UI 執行緒、start() 期間與之後使用；模組不要保存核心內部物件的指標，只保存這裡回傳的參考。
/// 還沒實作的服務也有入口（能力合約），方法由對應的核心任務補上。
class ModuleHost {
public:
    virtual ~ModuleHost() = default;

    /// 這份入口屬於哪個模組。
    virtual QString moduleCode() const = 0;

    virtual JobRunner& jobs() = 0;
    virtual LibraryQuery& library() = 0;
    virtual ModuleDatabase& database() = 0;
    virtual ModuleSettings& settings() = 0;
    virtual PlaybackService& playback() = 0;
    virtual OutputRegistry& outputs() = 0;
    virtual SourceRegistry& sources() = 0;
    virtual ProcessorChain& processors() = 0;
    virtual CoverService& covers() = 0;
    virtual UiRegistry& ui() = 0;
    virtual Navigator& navigator() = 0;
    virtual Dialogs& dialogs() = 0;
    virtual Notifier& notifier() = 0;
    virtual PlatformWindow& platformWindow() = 0;
    /// 已載入的外掛（`<auralis/plugins.h>`）；模組只取自己接手的類型（例：OUT 取 output）。
    virtual PluginRegistry& plugins() = 0;
};

/// 功能模組。停用或壞掉時程式照常運作：start() 丟出的例外由核心接住、記錄並把模組標記為失敗。
/// 執行緒：start()、stop() 都在 UI 執行緒呼叫。
class IModule {
public:
    virtual ~IModule() = default;
    virtual ModuleInfo info() const = 0;
    /// 註冊頁面、後端、音源、處理器、背景工作。只有 modules/<code>/enabled 不是 false 時才會呼叫。
    virtual void start(ModuleHost& host) = 0;
    /// 程式結束或停用時呼叫；只會對 start() 成功的模組呼叫。
    virtual void stop() noexcept = 0;
};

using ModuleFactory = std::unique_ptr<IModule> (*)();

/// 由 AURALIS_REGISTER_MODULE 在靜態初始化時呼叫，模組不要直接呼叫。
/// 回傳值只用來讓註冊放在命名空間層級的常數初始化裡。
bool registerModule(const char* code, ModuleFactory factory) noexcept;

} // namespace auralis

/// 靜態註冊模組：在模組的一個 .cpp 檔的命名空間層級寫一次。
/// Factory 是沒有捕捉的 lambda 或函式，回傳 std::unique_ptr<auralis::IModule>。
#define AURALIS_REGISTER_MODULE(Code, Factory)                                                     \
    namespace {                                                                                    \
    [[maybe_unused]] const bool auralisModuleRegistered_##Code =                                   \
        ::auralis::registerModule(#Code, Factory);                                                 \
    }
